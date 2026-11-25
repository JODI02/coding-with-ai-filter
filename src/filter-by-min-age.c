#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LINE_SIZE = 4096 };

static int parse_unsigned(const char *text, unsigned *value) {
  char *end;
  unsigned long parsed;

  if (!text || !*text)
    return 0;
  for (const char *p = text; *p; ++p) {
    if (!isdigit((unsigned char)*p))
      return 0;
  }

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno == ERANGE || *end != '\0' || parsed > UINT_MAX)
    return 0;

  *value = (unsigned)parsed;
  return 1;
}

static int parse_record(char *line, char *name, size_t name_size,
                        unsigned *age) {
  char *p = line;
  size_t name_length = 0;

  if (*p == '"') {
    ++p;
    while (*p) {
      if (*p == '"') {
        if (p[1] == '"') {
          if (name_length + 1 >= name_size)
            return 0;
          name[name_length++] = '"';
          p += 2;
        } else {
          ++p;
          break;
        }
      } else {
        if (name_length + 1 >= name_size)
          return 0;
        name[name_length++] = *p++;
      }
    }
    if (p[-1] != '"' || *p != ',')
      return 0;
    ++p;
  } else {
    char *comma = strchr(p, ',');
    if (!comma)
      return 0;
    name_length = (size_t)(comma - p);
    if (name_length >= name_size || memchr(p, '"', name_length))
      return 0;
    memcpy(name, p, name_length);
    p = comma + 1;
  }

  name[name_length] = '\0';
  if (name_length == 0)
    return 0;

  while (isspace((unsigned char)*p))
    ++p;
  if (!isdigit((unsigned char)*p))
    return 0;

  errno = 0;
  char *end;
  unsigned long parsed_age = strtoul(p, &end, 10);
  if (errno == ERANGE || parsed_age > UINT_MAX)
    return 0;
  while (isspace((unsigned char)*end))
    ++end;
  if (*end != '\0')
    return 0;

  *age = (unsigned)parsed_age;
  return 1;
}

static int write_record(FILE *ostream, const char *name, unsigned age) {
  int needs_quotes = strpbrk(name, ",\"") != NULL;
  if (needs_quotes && fputc('"', ostream) == EOF)
    return 0;

  for (const char *p = name; *p; ++p) {
    if (*p == '"' && fputc('"', ostream) == EOF)
      return 0;
    if (fputc(*p, ostream) == EOF)
      return 0;
  }

  if (needs_quotes && fputc('"', ostream) == EOF)
    return 0;
  return fprintf(ostream, ", %u\n", age) >= 0;
}

static int filter_stream(FILE *istream, FILE *ostream, unsigned minimum_age) {
  char line[LINE_SIZE];
  char name[LINE_SIZE];
  size_t line_no = 0;

  while (fgets(line, sizeof line, istream)) {
    ++line_no;
    size_t length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') {
      line[--length] = '\0';
    } else {
      int next = fgetc(istream);
      if (next != EOF) {
        while (next != '\n' && next != EOF)
          next = fgetc(istream);
        fprintf(stderr, "Line %zu: Row exceeds maximum length.\n", line_no);
        continue;
      }
    }

    if (length > 0 && line[length - 1] == '\r')
      line[--length] = '\0';

    unsigned age;
    if (!parse_record(line, name, sizeof name, &age)) {
      fprintf(stderr, "Line %zu: Invalid CSV row or age.\n", line_no);
      continue;
    }

    if (age >= minimum_age && !write_record(ostream, name, age)) {
      perror("Writing output");
      return 0;
    }
  }

  if (ferror(istream)) {
    perror("Reading input");
    return 0;
  }
  return 1;
}

int main(int argc, char *argv[]) {
  unsigned minimum_age;
  FILE *istream = stdin;
  FILE *ostream = stdout;
  int success;

  if (argc < 2 || argc > 4) {
    fprintf(stderr,
            "Usage: %s minimum-age [input-file] [output-file]\n"
            "Example: %s 18 input.csv output.csv\n",
            argv[0], argv[0]);
    return EXIT_FAILURE;
  }

  if (!parse_unsigned(argv[1], &minimum_age)) {
    fprintf(stderr, "Minimum age must be a non-negative integer.\n");
    return EXIT_FAILURE;
  }

  if (argc >= 3) {
    istream = fopen(argv[2], "r");
    if (!istream) {
      perror(argv[2]);
      return EXIT_FAILURE;
    }
  }

  if (argc == 4) {
    ostream = fopen(argv[3], "w");
    if (!ostream) {
      perror(argv[3]);
      if (istream != stdin)
        fclose(istream);
      return EXIT_FAILURE;
    }
  }

  success = filter_stream(istream, ostream, minimum_age);
  if (istream != stdin && fclose(istream) == EOF) {
    perror("Closing input");
    success = 0;
  }
  if (ostream != stdout && fclose(ostream) == EOF) {
    perror("Closing output");
    success = 0;
  }

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
