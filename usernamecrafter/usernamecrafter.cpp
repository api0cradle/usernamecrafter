#define _CRT_SECURE_NO_WARNINGS   // use portable fopen/strerror on MSVC without C4996 noise

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include <string>
#include <vector>
#include <unordered_set>

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
    std::unordered_set<std::string> seen;
    size_t estimate = (size_t)firstnames_count * (size_t)lastnames_count;
    if (estimate > 4000000) estimate = 4000000;   // cap the up-front reservation
    seen.reserve(estimate);

    std::string line;
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

            if (seen.insert(line).second) {
                fwrite(line.data(), 1, line.size(), output);
                fputc('\n', output);
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
