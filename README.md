# Card printing utility

A way to generate card layouts in 3x3 grids at various DPIs (300, 600, 1200) in a command-line tool.

Example showing the usage help:
```
./build/cardprint
```

```
Create sheets of cards arranged 3x3.
Input is a text file. See the test.txt example.
Output will be png [OUTPUT_PREFIX]XX.png. XX is the page number.
PAPER_SIZE AND PPI override any values defined in the input file.

Usage: cardprint INPUT_FILE [OUTPUT_PREFIX (default "page")] [PPI (300|600|1200) (default 300)] [PAP
ER_SIZE (A4|US) (default US)]
```

# Building
In a mingw64 environment or POSIX environment, you can just run the Makefile:
```
make
```

# Config file format
The config