# usernamecrafter
WARNING: This is one of my first C/C++ projects so don't expect much in terms of advanced code. 

User enumeration is becoming more and more important when we are performing password spraying. 
I saw myself needing a tool that can generate userlists fast and the result is this project.
The tool is flexible in the sense that you can specify formats how you see fit with the -f parameter.

Output is always **lowercase** and **deduplicated** - every line in the result is unique. Deduplication keeps one copy of each generated username in memory, so peak memory use scales with the size of the unique output.

Names are also **ASCII-folded** by default: accented letters are transliterated to their base letter and apostrophes/hyphens are dropped, so `José Müller` becomes `jose.muller`, `O'Brien` becomes `obrien`, and `Smith-Jones` becomes `smithjones` - matching how these typically appear as account names. Use `--no-fold` to keep the original characters (lowercased only). UTF-8 and Latin-1 input are both handled.

You can pass `-f` multiple times (or use `--formats-file`) to emit several naming conventions in a single run; results are deduplicated **across** all formats. This is handy when you don't yet know the target's convention.

I have a pre-compiled version in the x64\Release folder. 
If you don't trust me you can simply fire the project up in Visual Studio and compile it. 

## Speed
Example test from my system when combining a firstname list of 25502 lines, a lastname list containing 1000 and the pattern {firstname}.{lastname}@test.com.
This took 8.7 seconds and generated a wordlist file of 610mb containing 25502001 lines.

![image](https://github.com/user-attachments/assets/733a683d-9bed-4438-9c62-9d59add27e82)


## Usage
```
-h, --help        Show this help message and exit
-F, --firstnames  File with firstnames
-L, --lastnames   File with lastnames
-o, --output      Specify output file
-f, --format      Format pattern. May be given multiple times. {firstname}{lastname} or {firstname}(3).{lastname}
    --formats-file File with one format pattern per line (# comments and blank lines allowed)
    --no-fold      Keep original characters in names (skip accent transliteration)
-v, --version     Show program version

Example: usernamecrafter.exe -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}
Example: usernamecrafter.exe -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}.{lastname}@domain.com
Example: usernamecrafter.exe -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(1){lastname}
Example: usernamecrafter.exe -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}{lastname}(2)
Example: usernamecrafter.exe -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}(3){lastname}(3)

Several conventions at once (deduplicated across all of them):
Example: usernamecrafter.exe -F firstnames.txt -L lastnames.txt -o output.txt -f {firstname}.{lastname} -f {firstname}(1){lastname} -f {lastname}{firstname}(1)
```

If you want to do [a-z] you would need to generate a wordlist with a-z and use that as input to either firstname or lastname depending on how you want to use it. 
In Linux you can use this command:
```
echo {a..z} | tr ' ' '\n' > a-z.txt
```

## Other resources
This project does not contain any wordlists, so you have to find them yourselves. 
A great starting point is:
https://github.com/insidetrust/statistically-likely-usernames
