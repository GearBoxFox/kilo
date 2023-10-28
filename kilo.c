/**
 * A toy text editor built from Snaptokens text editor tutorial. 
 * Uses almost entirely VT100 escape sequences
*/

/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h> 
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.2"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 2

// key inputs
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}

// Highlight flags
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/*** data ***/

struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *single_line_comment_start;
  int flags;
};

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
} erow;

struct editorConfig {
  int cx, cy, rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  struct editorSyntax *syntax;
  struct termios orig_termios;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
};


struct abuf {
  char *b;
  int len;
};

enum editorKey{
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

enum editorHighlight{
  HL_NORMAL = 0,
  HL_STRING,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_NUMBER,
  HL_MATCH
};

struct editorConfig E;

/** File Types **/

char *C_HL_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cxx", ".hxx", NULL};
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** terminal ***/

// error handling function
void die(const char *s);

// enables canonical mode to the terminal
void disableRawMode();

// disables raw mode
void enableRawMode();

// low level keypress handling
int editorReadKey();

// gets the cursors position
int getCursorPosition(int *rows, int *cols);

// gets the size of the window
int getWindowSize(int *rows, int *cols);

/** Syntax Highlighting **/
void editorSelectSyntaxHighlighting();

// updated the highlight buffer to match the current render buffer
void editorUpdateSyntax(erow *row);

int editorSyntaxToColor(int hl);

int isSeperator(int c);

/** row operations **/
// Update character index to render index
int editorRowCxToRx(erow *row, int cx);

// converts from render index to character index
int editorRowRxToCx(erow *row, int rx);

// Update the render buffer from the raw character data
void editorUpdateRow(erow *row);

// appends a row to the global text buffer
void editorInsertRow(int at, char *s, size_t len);

// inserts a character into the row at a given position
void editorRowInsertCharacter(erow *row, int at, int c);

// concatinates two rows
void editorRowAppendString(erow *row, char *s, size_t len);

// simple low level backspacing on the erow
void editorRowDelChar(erow *row, int at);

// frees the string data from a row
void editorFreeRow(erow *row);


/** editor operations **/

// high level insert character
void editorInsertCharacter(int c);

// deletes a row
void editorDelRow(int at);

// adds a newline
void editorInsertNewline();

/** file I/O ***/

// converts the arrow of editor rows into a string
char *editorRowsToString(int *buflen);

// opens a file for viewing and editing
void editorOpen();

// saves the editor row buffers to a file
void editorSave();

/** search **/
// callback for incremental search
void editorFindCallback(char *query, int key);

// uses editor prompt to search through current file
void editorFind();

/*** append buffer ***/

// appends a string to the buffer
void abAppend(struct abuf *ab, const char *s, int len);

// frees a buffer
void abFree(struct abuf *ab);

/*** output ***/

// redraw the screen
void editorRefreshScreen();

// draws a status message
void editorSetStatusMessage(const char *fmt, ...);

// adds a status bar to the bottom of the screen
void editorDrawStatusBar(struct abuf *ab);

// message bar
void editorDrawMessageBar(struct abuf *ab);

// draws the text rows
void editorDrawRows(struct abuf *ab);

// scrolls the screen when needed
void editorScroll();

/*** input ***/

// prompt the user for input
char *editorPrompt(char *prompt, void (*callback)(char *, int));

// hanldes movement keypresses
void editorMoveCursor(int key);

// high level keypress handling
void editorProcessKeypress();

/*** init ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.numrows = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage(
    "HELP: Ctrl-F = find | Ctrl-S = save | Ctrl-Q = quit");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}

/*** Terminal ***/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear screen
  write(STDOUT_FILENO, "\x1b[H", 3); // move cursor to top right

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  // if reading an escape sequence
  if (c == '\x1b'){
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';

        if(seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '2': return END_KEY;
            case '3': return DEL_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) { // arrow key escape codes
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; 
  // returns cursor position in format \x1b[rows;colsR

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1; // parse rows and cols from output

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; //move the cursor to the bottom of screen
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/** Syntax Highlighting **/
void editorSelectSyntaxHighlighting() {
  E.syntax = NULL;
  if (E.filename == NULL) return;

  char *ext = strrchr(E.filename, '.');

  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {

      struct editorSyntax *s = &HLDB[j];

      unsigned int i = 0;
      while(s->filematch[i]) {
        int is_ext = (s->filematch[i][0] == '.');

        if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || // if extension match
            (!is_ext && strstr(E.filename, s->filematch[i]))) { // if name match
              E.syntax = s;

              int filerow; //update formatting
              for (filerow = 0; filerow < E.numrows; filerow++) {
                editorUpdateSyntax(&E.row[filerow]);
              }

              return;
            }
        i++;
      }
  }
}

void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (E.syntax == NULL) return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->single_line_comment_start;
  int scs_len = scs ? strlen(scs) : 0;

  int prev_sep = 1;
  int in_string=0;

  int i = 0;
  while(i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    // comment highlighting
    if (scs_len && !in_string) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    // string and char highlighting
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) { // if in a string
        row->hl[i] = HL_STRING;

        if (c =='\\' && i + 1 < row->size) {
          row->hl[i + 1] = HL_STRING;
          i+= 2;
          continue;
        }

        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++; 
          continue;
        }
      }
    }

    // number highlighting
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || 
        (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    //keyword highlighting
    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords [j]);
        int kw2 = keywords[j][klen - 1] == '|';

        if (kw2) klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) &&
         isSeperator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          continue;
        }
      }

      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = isSeperator(c);
    i++;
  }
}

int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_NUMBER: return 31;
    case HL_STRING: return 35;
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_MATCH: return 34;
    default: return 37;
  }
}

int isSeperator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>{};", c) != NULL;
}

/** row operations **/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx) return cx;
  }
  return cx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++) 
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }

  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorRowInsertCharacter(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row ->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->chars);
  free(row->render);
  free(row->hl);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

/** editor operations **/
void editorInsertCharacter(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertCharacter(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

/** file I/O ***/
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++) 
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

 void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  editorSelectSyntaxHighlighting();

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\r' ||
                          line[linelen -1] == '\n'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);

  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save Aborted");
      return;
    }
    editorSelectSyntaxHighlighting();
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/** search **/

void editorFindCallback(char *query, int key) { 
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl= NULL;
  }

  // Check arrow keys for handling search
  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1) direction = 1;
  int current = last_match; //current line to search
  int i;
  // searches for the first occurence of substring
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    // handles scrolling from top of document to bottom of document
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);

    if (match) {
      last_match = current;

      //TODO figure out how to make this not snap to top of screen
      int viewrow = current;
      if (viewrow < 0) viewrow = 0;
      if (viewrow > E.numrows) viewrow = E.numrows;

      E.cy = viewrow;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->size);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorFind() {
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/ARROWS/ENTER)", editorFindCallback);

  if (query) {
    free(query);
  } else {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/*** append buffer ***/

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/
void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  
  char status[80], rstatus[80];
  // left justified statusbar text
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", // orig format string
    E.filename ? E.filename : "[No Name]", /* filename */ E.numrows, // file length
    E.dirty ? "(modified)" : ""); // has unsaved changes?

  // right justified status bar text  
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    E.syntax ? E.syntax->filetype : "no ft", // filetype 
    E.cy + 1, E.numrows); // x, y position

  if (len > E.screencols) len = E.screencols; // size cap 
  abAppend(ab, status, len);

  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\n\r", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) { // draw rows
  int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) { // draw welcome message

        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);

        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;

        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }

        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);

      } /* end welcome message */ else {
        abAppend(ab, "~", 1);
      }
    } else {
      // Actaul text drawing happening here
      // figure out position and character here
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;

      // handle text rendering and highlighting
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;

      int j;
      for (j = 0; j < len; j++) { 
        if (iscntrl(c[j])) { // handle special characters

          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);

          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }

        } else if (hl[j] == HL_NORMAL) { // normal characters

          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (current_color != color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color); //format into buffer

            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      } // end for J aka draw row
      abAppend(ab, "\x1b[39m", 5); // reset formatting for next line
    } // end else

    abAppend(ab, "\x1b[K", 3); //clear screen from cursor

    abAppend(ab, "\r\n", 2);
  } // end for Y aka draw loop rows
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void editorRefreshScreen() {
  editorScroll(); 
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); // disable cursor
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, 
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); //enable cursor

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab);
}

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

    if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }

  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[buflen] = '\0';

  // loop while in the prompt
  while(1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    // Check for exiting prompt
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) { // Delete/backspace
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == '\x1b') { //delete
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') { // enter
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl('c') && c < 128) { // else allocate space for new character
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);
  }
}


void editorMoveCursor(int key) {
  erow *row = (E.cy > E.numrows) ? NULL : &E.row[E.cy];

  switch (key){
    case ARROW_LEFT:
      if (E.cx != 0){
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }

  if (E.cy < 0) E.cy = 0;
  if (E.cx < 0) E.cx = 0;
}

void editorProcessKeypress() {
  static int  quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
    case '\r':
      editorInsertNewline();
      break;

    case CTRL_KEY('q'): // quite keys
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!! File has unsaved changes. "
        "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }

      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    
    case CTRL_KEY('s'):
      editorSave();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      if (c == PAGE_UP) {
        E.cy = E.rowoff;
      } else if (c == PAGE_DOWN) {
        E.cy = E.rowoff + E.screenrows - 1;
        if ( E.cy > E.numrows) E.cy = E.numrows;
      }

      {
        int times = E.screenrows;
        while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    case END_KEY:
      if (E.cy < E.numrows) {
        E.cx  = E.row[E.cy].size;
      }
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;
    
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case ARROW_UP: // movement keys
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      editorInsertCharacter(c);
      break;
  }

  quit_times = KILO_QUIT_TIMES;
}
