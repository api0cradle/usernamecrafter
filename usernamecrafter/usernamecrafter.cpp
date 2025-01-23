#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#define MAX_NAME_LENGTH 100
#define VERSION "1.0"

// If compiling on MSVC (Visual Studio), map strdup to _strdup
#ifdef _MSC_VER
#define strdup _strdup
#endif

void generate_lists(FILE* firstnames_file, FILE* lastnames_file, FILE* output, const char* format) {
    clock_t start_time = clock();
    int lastnames_count = 0;
    char** lastnames = NULL;
    int firstnames_count = 0;
    char** firstnames = NULL;

    fseek(firstnames_file, 0, SEEK_SET);
    fseek(lastnames_file, 0, SEEK_SET);

    // Read all lastnames
    char lastname[MAX_NAME_LENGTH];
    while (fgets(lastname, sizeof(lastname), lastnames_file) != NULL) {
        lastname[strcspn(lastname, "\n")] = '\0';  // Remove newline
        char* temp = strdup(lastname);             // Use portable strdup
        if (temp == NULL) {
            fprintf(stderr, "Failed to allocate memory for lastnames\n");
            exit(EXIT_FAILURE);
        }
        char** temp_ptr = (char**)realloc(lastnames, (lastnames_count + 1) * sizeof(char*));
        if (temp_ptr == NULL) {
            fprintf(stderr, "Failed to reallocate memory for lastnames\n");
            free(temp);
            exit(EXIT_FAILURE);
        }
        lastnames = temp_ptr;
        lastnames[lastnames_count] = temp;
        lastnames_count++;
    }

    // Read all firstnames
    char firstname[MAX_NAME_LENGTH];
    while (fgets(firstname, sizeof(firstname), firstnames_file) != NULL) {
        firstname[strcspn(firstname, "\n")] = '\0'; // Remove newline
        char* temp = strdup(firstname);             // Use portable strdup
        if (temp == NULL) {
            fprintf(stderr, "Failed to allocate memory for firstnames\n");
            exit(EXIT_FAILURE);
        }
        char** temp_ptr = (char**)realloc(firstnames, (firstnames_count + 1) * sizeof(char*));
        if (temp_ptr == NULL) {
            fprintf(stderr, "Failed to reallocate memory for firstnames\n");
            free(temp);
            exit(EXIT_FAILURE);
        }
        firstnames = temp_ptr;
        firstnames[firstnames_count] = temp;
        firstnames_count++;
    }

    // Generate output based on format
    for (int i = 0; i < firstnames_count; i++) {
        for (int j = 0; j < lastnames_count; j++) {
            const char* ptr = format;
            while (*ptr) {
                if (strncmp(ptr, "{firstname}(", 12) == 0 && isdigit(ptr[12])) {
                    int len = atoi(ptr + 12);
                    fprintf(output, "%.*s", len, firstnames[i]);
                    while (*ptr && *ptr != ')') ptr++;
                    ptr++; // Skip ')'
                }
                else if (strncmp(ptr, "{firstname}", 11) == 0) {
                    fprintf(output, "%s", firstnames[i]);
                    ptr += 11;
                }
                else if (strncmp(ptr, "{lastname}(", 11) == 0 && isdigit(ptr[11])) {
                    int len = atoi(ptr + 11);
                    fprintf(output, "%.*s", len, lastnames[j]);
                    while (*ptr && *ptr != ')') ptr++;
                    ptr++; // Skip ')'
                }
                else if (strncmp(ptr, "{lastname}", 10) == 0) {
                    fprintf(output, "%s", lastnames[j]);
                    ptr += 10;
                }
                else {
                    fputc(*ptr, output);
                    ptr++;
                }
            }
            fprintf(output, "\n");
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
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}.{lastname}@domain.com\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(1){lastname}\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}(2)\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(3){lastname}(3)\n", program_name);
    printf("Example: %s -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(3)_{lastname}(3)@domain.com\n", program_name);
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
        // Use fopen instead of fopen_s
        outfile = fopen(output_file, "w");
        if (!outfile) {
            fprintf(stderr, "Error opening output file: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (firstnames_file) {
        printf("[+] - Firstnames file: %s\n", firstnames_file);
        // Use fopen instead of fopen_s
        firstnames = fopen(firstnames_file, "r");
        if (!firstnames) {
            fprintf(stderr, "Error opening firstnames file: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (lastnames_file) {
        printf("[+] - Lastnames file: %s\n", lastnames_file);
        // Use fopen instead of fopen_s
        lastnames = fopen(lastnames_file, "r");
        if (!lastnames) {
            fprintf(stderr, "Error opening lastnames file: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (format) {
        printf("[+] - Format: %s\n", format);
    }

    // If all parameters are missing, show help prompt
    if (!output_file && !firstnames_file && !lastnames_file && !format) {
        printf("No parameters specified - Restart with -h or --help for options\n");
        return 1;
    }

    // Check if files are opened successfully
    if (!firstnames) {
        fprintf(stderr, "Error: Firstnames file could not be opened\n");
        return 1;
    }
    if (!lastnames) {
        fprintf(stderr, "Error: Lastnames file could not be opened\n");
        return 1;
    }
    if (!outfile) {
        fprintf(stderr, "Error: Output file could not be opened\n");
        return 1;
    }

    // Generate the list
    generate_lists(firstnames, lastnames, outfile, format);

    // Close all open files
    fclose(lastnames);
    fclose(firstnames);
    fclose(outfile);

    return 0;
}

