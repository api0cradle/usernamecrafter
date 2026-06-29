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

// Append the lowercase ASCII transliteration of one Unicode code point to `out`.
// Accented Latin letters fold to their base letter (e -> e, ue -> u, ss -> ss, ...).
// Apostrophes and hyphens (ASCII and common Unicode variants) are dropped, so
// "O'Brien" -> "obrien" and "Smith-Jones" -> "smithjones". Unmapped non-ASCII code
// points are dropped (best-effort ASCII username, matching how AD folds names).
static void append_folded_cp(uint32_t cp, std::string& out) {
    if (cp < 0x80) {                       // plain ASCII
        if (cp == '\'' || cp == '-') return;
        out.push_back((char)tolower((int)cp));
        return;
    }
    switch (cp) {
    case 0x2018: case 0x2019: return;      // ' ' curly single quotes
    case 0x2010: case 0x2011: case 0x2012: // hyphen / non-breaking hyphen / figure dash
    case 0x2013: case 0x2014: return;      // en dash / em dash
    case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
    case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
    case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105: out.push_back('a'); return;
    case 0x00C6: case 0x00E6: out.append("ae"); return;
    case 0x00C7: case 0x00E7:
    case 0x0106: case 0x0107: case 0x0108: case 0x0109: case 0x010A: case 0x010B: case 0x010C: case 0x010D: out.push_back('c'); return;
    case 0x00D0: case 0x00F0:
    case 0x010E: case 0x010F: case 0x0110: case 0x0111: out.push_back('d'); return;
    case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
    case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
    case 0x0112: case 0x0113: case 0x0114: case 0x0115: case 0x0116: case 0x0117:
    case 0x0118: case 0x0119: case 0x011A: case 0x011B: out.push_back('e'); return;
    case 0x011C: case 0x011D: case 0x011E: case 0x011F: case 0x0120: case 0x0121: case 0x0122: case 0x0123: out.push_back('g'); return;
    case 0x0124: case 0x0125: case 0x0126: case 0x0127: out.push_back('h'); return;
    case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
    case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
    case 0x0128: case 0x0129: case 0x012A: case 0x012B: case 0x012C: case 0x012D:
    case 0x012E: case 0x012F: case 0x0130: case 0x0131: out.push_back('i'); return;
    case 0x0134: case 0x0135: out.push_back('j'); return;
    case 0x0136: case 0x0137: out.push_back('k'); return;
    case 0x0139: case 0x013A: case 0x013B: case 0x013C: case 0x013D: case 0x013E:
    case 0x0141: case 0x0142: out.push_back('l'); return;
    case 0x00D1: case 0x00F1:
    case 0x0143: case 0x0144: case 0x0145: case 0x0146: case 0x0147: case 0x0148: out.push_back('n'); return;
    case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
    case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
    case 0x014C: case 0x014D: case 0x014E: case 0x014F: case 0x0150: case 0x0151: out.push_back('o'); return;
    case 0x0152: case 0x0153: out.append("oe"); return;
    case 0x0154: case 0x0155: case 0x0156: case 0x0157: case 0x0158: case 0x0159: out.push_back('r'); return;
    case 0x015A: case 0x015B: case 0x015C: case 0x015D: case 0x015E: case 0x015F: case 0x0160: case 0x0161: out.push_back('s'); return;
    case 0x00DF: out.append("ss"); return;
    case 0x00DE: case 0x00FE: out.append("th"); return;
    case 0x0162: case 0x0163: case 0x0164: case 0x0165: case 0x0166: case 0x0167: out.push_back('t'); return;
    case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
    case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
    case 0x0168: case 0x0169: case 0x016A: case 0x016B: case 0x016C: case 0x016D:
    case 0x016E: case 0x016F: case 0x0170: case 0x0171: case 0x0172: case 0x0173: out.push_back('u'); return;
    case 0x0174: case 0x0175: out.push_back('w'); return;
    case 0x00DD: case 0x00FD: case 0x00FF:
    case 0x0176: case 0x0177: case 0x0178: out.push_back('y'); return;
    case 0x0179: case 0x017A: case 0x017B: case 0x017C: case 0x017D: case 0x017E: out.push_back('z'); return;
    default: return;                       // unmapped non-ASCII: drop
    }
}

// Fold a name into a lowercase ASCII username candidate. Input is decoded as UTF-8; a byte that
// is not part of a valid UTF-8 sequence is treated as a Latin-1 code point, so both UTF-8 and
// Latin-1/Windows-1252 name files transliterate sensibly. (The NUL terminator stops every lookahead
// because an invalid/short sequence falls through to the single-byte branch.)
static std::string fold_name(const char* s) {
    std::string out;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        unsigned char c = *p;
        uint32_t cp;
        if (c < 0x80) {
            cp = c; p += 1;
        }
        else if ((c >> 5) == 0x6 && (p[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
        }
        else if ((c >> 4) == 0xE && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
        }
        else if ((c >> 3) == 0x1E && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
        }
        else {
            cp = c; p += 1;                 // invalid UTF-8 -> treat as Latin-1
        }
        append_folded_cp(cp, out);
    }
    return out;
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

// Read every line from `file` into a heap array of C strings, lowercased (and, when `fold` is set,
// transliterated to ASCII with apostrophes/hyphens stripped). Returns the array and sets *count.
// Grows geometrically to avoid O(n) reallocs. Exits on OOM.
static char** read_names(FILE* file, int* count, bool fold) {
    char** names = NULL;
    int n = 0;
    int cap = 0;
    char line[MAX_NAME_LENGTH];

    fseek(file, 0, SEEK_SET);
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';   // strip trailing CR and/or LF
        char* dup;
        if (fold) {
            std::string folded = fold_name(line);
            dup = strdup(folded.c_str());
        }
        else {
            dup = strdup(line);
            if (dup != NULL) str_tolower(dup);
        }
        if (dup == NULL) {
            fprintf(stderr, "Failed to allocate memory for names\n");
            exit(EXIT_FAILURE);
        }

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

void generate_lists(FILE* firstnames_file, FILE* lastnames_file, FILE* output,
                    const std::vector<std::string>& formats, bool fold) {
    clock_t start_time = clock();

    // Parse every format once up front.
    std::vector<std::vector<Token>> token_lists;
    token_lists.reserve(formats.size());
    for (const std::string& fmt : formats) {
        bool has_name_placeholder = false;
        token_lists.push_back(parse_format(fmt.c_str(), &has_name_placeholder));
        if (!has_name_placeholder) {
            fprintf(stderr, "[!] - Warning: format \"%s\" has no {firstname}/{lastname} placeholder; "
                            "it contributes only a single constant line.\n", fmt.c_str());
        }
    }

    // Load the name lists (lowercased, and transliterated to ASCII unless --no-fold was given).
    int firstnames_count = 0;
    int lastnames_count = 0;
    char** firstnames = read_names(firstnames_file, &firstnames_count, fold);
    char** lastnames = read_names(lastnames_file, &lastnames_count, fold);

    // Dedup every emitted line across ALL formats. `seen` holds one copy of each unique username.
    size_t estimate = formats.size() * (size_t)firstnames_count * (size_t)lastnames_count;
    LineSet seen(estimate);

    std::string line;
    line.reserve(64);
    unsigned long long unique_count = 0;
    unsigned long long dup_count = 0;

    for (const std::vector<Token>& tokens : token_lists) {
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
    printf("  -h, --help        Show this help message and exit\n");
    printf("  -F, --firstnames  File with firstnames\n");
    printf("  -L, --lastnames   File with lastnames\n");
    printf("  -o, --output      Specify output file\n");
    printf("  -f, --format      Format pattern. May be given multiple times to emit several\n");
    printf("                    conventions at once. {firstname}{lastname} or {firstname}(3).{lastname}\n");
    printf("      --formats-file  File with one format pattern per line (# comments allowed)\n");
    printf("      --no-fold     Keep original characters in names (skip accent transliteration)\n");
    printf("  -v, --version     Show program version\n\n");
    printf("Output is always lowercase, deduplicated (across all formats), and ASCII-folded by\n");
    printf("default (e.g. Jose Muller with accents -> jose.muller, O'Brien -> obrien).\n\n");
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}.{lastname}@domain.com\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(1){lastname}\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}(2)\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(3){lastname}(3)\n", program_name);
    printf("Multi:   %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}.{lastname} -f {firstname}(1){lastname} -f {lastname}{firstname}(1)\n", program_name);
}

// Open `path` with `mode`; on failure print an error mentioning `label` and return NULL.
static FILE* open_or_report(const char* path, const char* mode, const char* label) {
    FILE* f = fopen(path, mode);
    if (f == NULL) {
        fprintf(stderr, "Error opening %s file: %s\n", label, strerror(errno));
    }
    return f;
}

// Append each format line from `path` to `formats`. Blank lines and lines starting with '#'
// (after leading whitespace) are skipped. Returns false if the file cannot be opened.
static bool load_formats_file(const char* path, std::vector<std::string>& formats) {
    FILE* f = open_or_report(path, "r", "formats");
    if (f == NULL) return false;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f) != NULL) {
        buf[strcspn(buf, "\r\n")] = '\0';
        char* p = buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;
        formats.push_back(p);
    }
    fclose(f);
    return true;
}

int main(int argc, char* argv[]) {
    // Default values for options
    char* output_file = NULL;
    char* firstnames_file = NULL;
    char* lastnames_file = NULL;
    std::vector<std::string> formats;   // one or more -f / --formats-file patterns
    bool fold = true;                   // ASCII-fold names unless --no-fold is given

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
                formats.push_back(argv[++i]);
            }
            else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "-ff") == 0 || strcmp(argv[i], "--formats-file") == 0) {
            if (i + 1 < argc) {
                if (!load_formats_file(argv[++i], formats)) {
                    return 1;
                }
            }
            else {
                fprintf(stderr, "Error: Missing value for %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--no-fold") == 0) {
            fold = false;
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

    for (const std::string& fmt : formats) {
        printf("[+] - Format: %s\n", fmt.c_str());
    }
    printf("[+] - Name folding: %s\n", fold ? "enabled (accents -> ASCII)" : "disabled");

    if (!output_file && !firstnames_file && !lastnames_file && formats.empty()) {
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
    if (formats.empty()) {
        fprintf(stderr, "Error: No format specified (use -f or --formats-file). See -h for examples.\n");
        return 1;
    }

    generate_lists(firstnames, lastnames, outfile, formats, fold);
    fclose(lastnames);
    fclose(firstnames);
    fclose(outfile);

    return 0;
}
