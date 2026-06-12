#define _CRT_SECURE_NO_WARNINGS   // use portable fopen/strerror on MSVC without C4996 noise

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include <stdint.h>

#include <string>
#include <vector>

#ifdef _MSC_VER
#define strdup _strdup   // MSVC names the POSIX function _strdup
#endif

#define MAX_NAME_LENGTH 100
#define VERSION "1.0"

// Lowercase a C string in place. Cast to unsigned char to avoid UB on bytes > 0x7F.
static void str_tolower(char* s) {
    for (; *s; ++s) {
        *s = (char)tolower((unsigned char)*s);
    }
}

// A pre-parsed piece of the format string.
enum TokenType {
    TOK_FIRSTNAME,    // full first name
    TOK_LASTNAME,     // full last name
    TOK_FIRSTNAME_N,  // first `len` chars of the first name
    TOK_LASTNAME_N,   // first `len` chars of the last name
    TOK_LITERAL       // literal text (already lowercased)
};

struct Token {
    TokenType type;
    int len = 0;            // used by TOK_FIRSTNAME_N / TOK_LASTNAME_N
    std::string literal;    // used by TOK_LITERAL
};

// Parse the format string once into a token list. Literal runs are coalesced into a single
// token and lowercased here, so the hot generation loop never re-parses or re-cases anything.
// *has_name_placeholder is set if at least one {firstname}/{lastname} token is present.
static std::vector<Token> parse_format(const char* format, bool* has_name_placeholder) {
    std::vector<Token> tokens;
    *has_name_placeholder = false;

    std::string literal;
    auto flush_literal = [&]() {
        if (!literal.empty()) {
            Token t;
            t.type = TOK_LITERAL;
            t.literal = literal;
            tokens.push_back(t);
            literal.clear();
        }
    };

    const char* ptr = format;
    while (*ptr) {
        // Precedence mirrors the original parser: truncated placeholder, then full, then literal.
        if (strncmp(ptr, "{firstname}(", 12) == 0 && isdigit((unsigned char)ptr[12])) {
            flush_literal();
            Token t; t.type = TOK_FIRSTNAME_N; t.len = atoi(ptr + 12);
            tokens.push_back(t);
            *has_name_placeholder = true;
            while (*ptr && *ptr != ')') ptr++;
            if (*ptr == ')') ptr++;   // guard against a missing ')' (don't run past the NUL)
        }
        else if (strncmp(ptr, "{firstname}", 11) == 0) {
            flush_literal();
            Token t; t.type = TOK_FIRSTNAME;
            tokens.push_back(t);
            *has_name_placeholder = true;
            ptr += 11;
        }
        else if (strncmp(ptr, "{lastname}(", 11) == 0 && isdigit((unsigned char)ptr[11])) {
            flush_literal();
            Token t; t.type = TOK_LASTNAME_N; t.len = atoi(ptr + 11);
            tokens.push_back(t);
            *has_name_placeholder = true;
            while (*ptr && *ptr != ')') ptr++;
            if (*ptr == ')') ptr++;   // guard against a missing ')'
        }
        else if (strncmp(ptr, "{lastname}", 10) == 0) {
            flush_literal();
            Token t; t.type = TOK_LASTNAME;
            tokens.push_back(t);
            *has_name_placeholder = true;
            ptr += 10;
        }
        else {
            literal += (char)tolower((unsigned char)*ptr);
            ptr++;
        }
    }
    flush_literal();
    return tokens;
}

// Read every line from `file` into a heap array of lowercased C strings.
// Returns the array and sets *count. Grows geometrically to avoid O(n) reallocs. Exits on OOM.
static char** read_names(FILE* file, int* count) {
    char** names = NULL;
    int n = 0;
    int cap = 0;
    char line[MAX_NAME_LENGTH];

    fseek(file, 0, SEEK_SET);
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';   // strip trailing CR and/or LF
        char* dup = strdup(line);
        if (dup == NULL) {
            fprintf(stderr, "Failed to allocate memory for names\n");
            exit(EXIT_FAILURE);
        }
        str_tolower(dup);

        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            char** tmp = (char**)realloc(names, (size_t)cap * sizeof(char*));
            if (tmp == NULL) {
                fprintf(stderr, "Failed to reallocate memory for names\n");
                free(dup);
                exit(EXIT_FAILURE);
            }
            names = tmp;
        }
        names[n++] = dup;
    }

    *count = n;
    return names;
}

// Exact, allocation-light dedup of generated lines.
//
// A std::unordered_set<std::string> works but is slow at scale: it heap-allocates a node (and,
// for lines longer than the small-string buffer, a separate char buffer) for every unique line,
// then chases pointers on every lookup. For tens of millions of lines that allocation churn and
// the cache-unfriendly layout dominate runtime.
//
// Instead we keep unique line bytes back-to-back in big arena blocks (one bump-allocator, almost
// no per-line malloc) and index them with a flat open-addressing table of pointers. Lookups stay
// in contiguous memory and comparisons are exact byte-for-byte, so there is no hash-collision risk
// of dropping a genuinely distinct username.
class LineSet {
public:
    explicit LineSet(size_t expected) {
        size_t need = (size_t)(expected / kLoadFactor) + 1;
        // Cap the up-front table so an absurd estimate can't try to allocate many GB; grow() covers
        // anything larger by rehashing as needed.
        const size_t kMaxInitialSlots = (size_t)1 << 27;  // ~1 GiB of pointers
        if (need > kMaxInitialSlots) need = kMaxInitialSlots;
        cap_ = 16;
        while (cap_ < need) cap_ <<= 1;
        mask_ = cap_ - 1;
        table_ = (char**)calloc(cap_, sizeof(char*));
        if (table_ == NULL) {
            fprintf(stderr, "Failed to allocate dedup table\n");
            exit(EXIT_FAILURE);
        }
        grow_at_ = (size_t)(cap_ * kLoadFactor);
    }

    ~LineSet() {
        for (char* block : blocks_) free(block);
        free(table_);
    }

    // Returns true if (s, n) was not seen before (and records it); false if it is a duplicate.
    bool insert(const char* s, size_t n) {
        uint64_t h = hash_bytes(s, n);
        size_t pos = h & mask_;
        while (table_[pos] != NULL) {
            if (record_equals(table_[pos], s, n)) return false;
            pos = (pos + 1) & mask_;
        }
        if (count_ >= grow_at_) {
            grow();
            pos = h & mask_;
            while (table_[pos] != NULL) pos = (pos + 1) & mask_;
        }
        table_[pos] = store(s, n);
        count_++;
        return true;
    }

private:
    static const size_t kBlockSize = (size_t)16 << 20;  // arena block granularity
    static constexpr double kLoadFactor = 0.7;

    // Record layout in the arena: [uint32_t length][bytes].
    static uint64_t hash_bytes(const char* s, size_t n) {  // FNV-1a (64-bit)
        uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; i < n; i++) {
            h ^= (unsigned char)s[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    static bool record_equals(const char* rec, const char* s, size_t n) {
        uint32_t len;
        memcpy(&len, rec, sizeof(uint32_t));
        return len == n && memcmp(rec + sizeof(uint32_t), s, n) == 0;
    }

    char* store(const char* s, size_t n) {
        size_t rec = sizeof(uint32_t) + n;
        if (block_left_ < rec) {
            size_t bs = rec > kBlockSize ? rec : kBlockSize;
            block_ = (char*)malloc(bs);
            if (block_ == NULL) {
                fprintf(stderr, "Failed to allocate dedup arena\n");
                exit(EXIT_FAILURE);
            }
            blocks_.push_back(block_);
            block_left_ = bs;
        }
        char* p = block_;
        uint32_t len = (uint32_t)n;
        memcpy(p, &len, sizeof(uint32_t));
        memcpy(p + sizeof(uint32_t), s, n);
        block_ += rec;
        block_left_ -= rec;
        return p;
    }

    void grow() {
        size_t ncap = cap_ << 1;
        size_t nmask = ncap - 1;
        char** nt = (char**)calloc(ncap, sizeof(char*));
        if (nt == NULL) {
            fprintf(stderr, "Failed to grow dedup table\n");
            exit(EXIT_FAILURE);
        }
        for (size_t i = 0; i < cap_; i++) {
            char* rec = table_[i];
            if (rec == NULL) continue;
            uint32_t len;
            memcpy(&len, rec, sizeof(uint32_t));
            uint64_t h = hash_bytes(rec + sizeof(uint32_t), len);
            size_t pos = h & nmask;
            while (nt[pos] != NULL) pos = (pos + 1) & nmask;
            nt[pos] = rec;
        }
        free(table_);
        table_ = nt;
        cap_ = ncap;
        mask_ = nmask;
        grow_at_ = (size_t)(cap_ * kLoadFactor);
    }

    std::vector<char*> blocks_;
    char* block_ = NULL;
    size_t block_left_ = 0;

    char** table_ = NULL;
    size_t cap_ = 0;
    size_t mask_ = 0;
    size_t count_ = 0;
    size_t grow_at_ = 0;
};

void generate_lists(FILE* firstnames_file, FILE* lastnames_file, FILE* output, const char* format) {
    clock_t start_time = clock();

    // Parse the format a single time up front.
    bool has_name_placeholder = false;
    std::vector<Token> tokens = parse_format(format, &has_name_placeholder);
    if (!has_name_placeholder) {
        fprintf(stderr, "[!] - Warning: format has no {firstname}/{lastname} placeholder; "
                        "every combination is identical, so only one line will be written.\n");
    }

    // Load the name lists (already trimmed + lowercased).
    int firstnames_count = 0;
    int lastnames_count = 0;
    char** firstnames = read_names(firstnames_file, &firstnames_count);
    char** lastnames = read_names(lastnames_file, &lastnames_count);

    // Dedup every emitted line. `seen` holds one copy of each unique username.
    size_t estimate = (size_t)firstnames_count * (size_t)lastnames_count;
    LineSet seen(estimate);

    std::string line;
    line.reserve(64);
    unsigned long long unique_count = 0;
    unsigned long long dup_count = 0;

    for (int i = 0; i < firstnames_count; i++) {
        for (int j = 0; j < lastnames_count; j++) {
            line.clear();
            for (const Token& t : tokens) {
                switch (t.type) {
                case TOK_FIRSTNAME:
                    line.append(firstnames[i]);
                    break;
                case TOK_LASTNAME:
                    line.append(lastnames[j]);
                    break;
                case TOK_FIRSTNAME_N: {
                    size_t avail = strlen(firstnames[i]);
                    size_t take = (t.len < 0) ? 0 : (size_t)t.len;
                    if (take > avail) take = avail;
                    line.append(firstnames[i], take);
                    break;
                }
                case TOK_LASTNAME_N: {
                    size_t avail = strlen(lastnames[j]);
                    size_t take = (t.len < 0) ? 0 : (size_t)t.len;
                    if (take > avail) take = avail;
                    line.append(lastnames[j], take);
                    break;
                }
                case TOK_LITERAL:
                    line.append(t.literal);
                    break;
                }
            }

            line.push_back('\n');
            if (seen.insert(line.data(), line.size())) {
                fwrite(line.data(), 1, line.size(), output);
                unique_count++;
            }
            else {
                dup_count++;
            }
        }
    }

    // Free allocated memory
    for (int i = 0; i < firstnames_count; i++) {
        free(firstnames[i]);
    }
    free(firstnames);

    for (int i = 0; i < lastnames_count; i++) {
        free(lastnames[i]);
    }
    free(lastnames);

    clock_t end_time = clock();
    double execution_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("[+] - Generating file took: %.2f seconds\n", execution_time);
    printf("[+] - Wrote %llu unique usernames (skipped %llu duplicates)\n", unique_count, dup_count);
}

void display_help(const char* program_name) {
    printf("                    =====\n");
    printf("                    |@@@|\n");
    printf("        John Smith >|***|> J.Smith@dom.com\n");
    printf("   =================|===|=================\n");
    printf("    |___|___|___|___|+++|___|___|___|___|  \n");
    printf("            UsernameCrafter v%s\n            Oddvar Moe @oddvarmoe\n\n", VERSION);
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -h, --help       Show this help message and exit\n");
    printf("  -F, --firstnames File with firstnames\n");
    printf("  -L, --lastnames  File with lastnames\n");
    printf("  -o, --output     Specify output file\n");
    printf("  -f, --format     Specify format. {firstname}{lastname} or {firstname}(3).{lastname}\n");
    printf("  -v, --version    Show program version\n\n");
    printf("Output is always lowercase and deduplicated.\n\n");
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}.{lastname}@domain.com\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(1){lastname}\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}(2)\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(3){lastname}(3)\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(3)_{lastname}(3)@domain.com\n", program_name);
}

// Open `path` with `mode`; on failure print an error mentioning `label` and return NULL.
static FILE* open_or_report(const char* path, const char* mode, const char* label) {
    FILE* f = fopen(path, mode);
    if (f == NULL) {
        fprintf(stderr, "Error opening %s file: %s\n", label, strerror(errno));
    }
    return f;
}

int main(int argc, char* argv[]) {
    // Default values for options
    char* output_file = NULL;
    char* firstnames_file = NULL;
    char* lastnames_file = NULL;
    char* format = NULL;

    // Declare file pointers
    FILE* outfile = NULL;
    FILE* firstnames = NULL;
    FILE* lastnames = NULL;

    // Iterate through arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            display_help(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                output_file = argv[++i];
            }
            else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-F") == 0 || strcmp(argv[i], "--firstnames") == 0) {
            if (i + 1 < argc) {
                firstnames_file = argv[++i];
            }
            else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--lastnames") == 0) {
            if (i + 1 < argc) {
                lastnames_file = argv[++i];
            }
            else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--format") == 0) {
            if (i + 1 < argc) {
                format = argv[++i];
            }
            else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("Version %s\n", VERSION);
            return 0;
        }
        else {
            fprintf(stderr, "Error: Unknown option %s\n", argv[i]);
            return 1;
        }
    }

    // Display banner and parsed options
    printf("                    =====\n");
    printf("                    |@@@|\n");
    printf("        John Smith >|***|> J.Smith@dom.com\n");
    printf("   =================|===|=================\n");
    printf("    |___|___|___|___|+++|___|___|___|___|  \n");
    printf("            UsernameCrafter v%s\n            Oddvar Moe @oddvarmoe\n\n", VERSION);

    if (output_file) {
        printf("[+] - Output file: %s\n", output_file);
        outfile = open_or_report(output_file, "w", "output");
        if (outfile == NULL) {
            exit(EXIT_FAILURE);
        }
        // Large fully-buffered output cuts write syscalls when generating huge lists. Passing NULL
        // lets the C runtime own the buffer, so it stays valid until fclose() and is freed there.
        setvbuf(outfile, NULL, _IOFBF, 1 << 24);   // 16 MiB
    }

    if (firstnames_file) {
        printf("[+] - Firstnames file: %s\n", firstnames_file);
        firstnames = open_or_report(firstnames_file, "r", "firstnames");
        if (firstnames == NULL) {
            exit(EXIT_FAILURE);
        }
    }

    if (lastnames_file) {
        printf("[+] - Lastnames file: %s\n", lastnames_file);
        lastnames = open_or_report(lastnames_file, "r", "lastnames");
        if (lastnames == NULL) {
            exit(EXIT_FAILURE);
        }
    }

    if (format) {
        printf("[+] - Format: %s\n", format);
    }

    if (!output_file && !firstnames_file && !lastnames_file && !format) {
        printf("No parameters specified - Restart with -h or --help for options\n");
        return 1;
    }

    // Check that everything we need was supplied and opened successfully
    if (firstnames == NULL) {
        fprintf(stderr, "Error: Firstnames file could not be opened\n");
        return 1;
    }
    if (lastnames == NULL) {
        fprintf(stderr, "Error: Lastnames file could not be opened\n");
        return 1;
    }
    if (outfile == NULL) {
        fprintf(stderr, "Error: Output file could not be opened\n");
        return 1;
    }
    if (format == NULL) {
        fprintf(stderr, "Error: No format specified (use -f). See -h for examples.\n");
        return 1;
    }

    generate_lists(firstnames, lastnames, outfile, format);
    fclose(lastnames);
    fclose(firstnames);
    fclose(outfile);

    return 0;
}
