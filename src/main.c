/* Simplr -  editor made in one month using Salvatore Sanfilippo's tutorial */
/* This program DOES NOT depend on any libcurses and emits VT100 escapes directly on terminal. */
/* I recommend every C beginner to do this project as it advances your knowledge in C and programming in general. */
/* Visit Salvatore's GitHub page and website where he published the tutorial */
/* Github: https://github.com/antirez */
/* Website: https://viewsourcecode.org/snaptoken/kilo/ */
/* More features soon! */
/* Made by: ch4tic */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/* LIBRARY INCLUDE  AND DEFINES*/
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

#define SIMPLR_VERSION "v.1"
#define SIMPLR_TAB_STOP 8
#define SIMPLR_QUIT_TIMES 1

#define CTRL_KEY(k) ((k) & 0x1f)


enum editorKey
{
	/* Setting arrow keys*/
	BACKSPACE = 127, 
	LEFT = 1000, 
	RIGHT, 
	UP,
	DOWN,


	PAGE_UP,
	PAGE_DOWN,
	DEL,
	HOME,
	END
};

/* ====== DATA ======*/
/* Structure made for storing a line of text as a pointer */
typedef struct editor_row
{
	int size;
	char *chars;
	int rsize; 
	char *render;
} editor_row;

struct editorConfig
{
	int cx, cy; 
	int coloff;
	int rx;	
	int rowoff; /* This variable will come in useful when building vertical scroll feature*/
	int screenrows;
	int screencols;
	int numrows;
	editor_row *row;
	int dirty_flag;
	char *filename;
	char status_message[80];
	time_t status_message_time;
	struct termios original_termios;
};

struct editorConfig conf; 

/* ====== PROTOTYPES ======*/
void statusMessage(const char *fmt, ...);
void clearScreen();
char *editorPrompt(char *prompt);

/* Function to output all errors that occurr*/
void errorHandling(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s); 
	exit(1);
}

/* ====== TERMINAL ======*/
/*Function to restore users original terminal attributes at exit*/
void disableRawMode()
{
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &conf.original_termios) == -1)
	{
		errorHandling("tcsetattr");
	}
}

void enableRawMode()
{
	if(tcgetattr(STDIN_FILENO, &conf.original_termios) == -1)
	{
		errorHandling("tcgetattr");
	}
	
	/*Disabling raw mode at exit*/
	atexit(disableRawMode);	 
	struct termios rawmode = conf.original_termios; 
	/*Disabling CTRL+Q and CTRL+S*/
   	rawmode.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	rawmode.c_iflag &= !(ICRNL | IXON);
	rawmode.c_oflag &= !(OPOST);
	rawmode.c_cflag |= (CS8);
	/*Disabling canonical/cooked mode(reading input byte-by-byte instead of line-by-line) and CTRL+Z/CTRL+C as they can terminate the editor or suspend it to background*/
	rawmode.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	/*Setting a timeout for read function*/
	rawmode.c_cc[VMIN] = 0;
  	rawmode.c_cc[VTIME] = 1;
	/*Setting terminal parameters using tcsetattr() into termios struct*/
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawmode) == -1)
	{
		errorHandling("tcsetattr");
	}
}

/*Function for listening for one keypress and returning it*/
int editorReadKey()
{
	int nread;
	char c; 
	while((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		if(nread == -1 && errno != EAGAIN)
		{
			errorHandling("read");
		}
	}
	/*Reading arrow keys and modifying the function to read escape chars as single keypresses*/
	/*Here the program is handling ESCAPE sequences such as PAGE_UP/DOWN or HOME/END*/
	if(c == '\x1b')
	{
		char sequence[3];

		if(read(STDIN_FILENO, &sequence[0], 1) != 1)
		{
			return '\x1b';
		}
		if(read(STDIN_FILENO, &sequence[1], 1) != 1)
		{
			return '\x1b';
		}
		if(sequence[0] == '[')
		{
			if (sequence[1] >= '0' && sequence[1] <= '9') {
        			if (read(STDIN_FILENO, &sequence[2], 1) != 1) return '\x1b';
        			if (sequence[2] == '~') {
          				switch (sequence[1]) {
						case '1': 
							return HOME;
						case '3': 
							return DEL;
						case '4': 
							return END;
						case '5': 
							return PAGE_UP;
            					case '6': 
							return PAGE_DOWN;
						case '7': 
							return HOME;
						case '8': 
							return END;
          				}
        		}
      			}else
		       	{
				switch(sequence[1])
				{
					case 'A':
						return UP;
					case 'B':
						return DOWN;
					case 'C':
						return RIGHT;
					case 'D':
						return LEFT;
					case 'H':
						return HOME;
					case 'F':
						return END;
				}
			}
		}else if(sequence[0] == 'O')
		{
			switch(sequence[1])
			{
				case 'H':
					return HOME;
				case 'F':
					return END;
			}
		}
		return '\x1b';
	}else
	{
		return c;
	}
}

int getCursorPosition(int *rows, int *cols)
{
	char buf[32]; 
	unsigned int i = 0;
	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
	{
		return -1; 
	}
	while(i < sizeof(buf) - 1)
	{
		if(read(STDIN_FILENO, &buf[i], 1) != 1)
		{
			break; 
		}
		if(buf[i] == 'R')
		{
			break; 
		}
		i++;
	}
	buf[i] = '\0';
	if(buf[0] != '\x1b' || buf[1] != '[')
	{
		return -1; 
	}
	if(sscanf(&buf[2], "%d;%d", rows, cols) != 2)
	{
		return -1;
	}
	return 0;
}

/*Function for getting real terminal window size*/
int getWindowSize(int *rows, int *cols)
{
	struct winsize ws; 

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) 
	{
		if(write(STDOUT_FILENO, "\x1b[999C\x1n[999B", 12) != 12)
		{
			return -1; 
		}
		return getCursorPosition(rows, cols);
	}else
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/* ====== ROW OPERATIONS ======*/

/* Converting cx to rx to find out how many columns the user's cursor is to the left of the next tab stop(4) */
int editorRowCxToRx(editor_row *row, int cx)
{
	int rx = 0;
	int j;
	for(j = 0; j < cx; j++)
	{
		if(row->chars[j] == '\t')
			rx += (SIMPLR_TAB_STOP - 1) - (rx % SIMPLR_TAB_STOP);
		rx++;
	}
	return rx;
}

void editorRowUpdate(editor_row *row)
{
	int tabs = 0;
	int j; 
	for(j = 0; j < row->size; j++)
	{
		if(row->chars[j] == '\t') tabs++;
	}
	free(row->render);
	row->render = malloc(row->size + tabs*(SIMPLR_TAB_STOP -1) + 1);
	int idx = 0;
	for(j= 0; j < row->size; j++)
	{
		if(row->chars[j] == '\t')
		{
			row->render[idx++] = row->chars[j];
			while(idx % 8 != 0)
			{
				row->render[idx++] = ' ';
			}
			while (idx % SIMPLR_TAB_STOP != 0)
			{
				row->render[idx++] = ' ';
			}
		}else
		{
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len)
{
	if(at < 0 || at > conf.numrows)
	{
		return; 
	}
	conf.row = realloc(conf.row, sizeof(editor_row) * (conf.numrows + 1));
	memmove(&conf.row[at + 1], &conf.row[at], sizeof(editor_row) * (conf.numrows - at));
	conf.row[at].size = len;
	conf.row[at].chars = malloc(len + 1);
	memcpy(conf.row[at].chars, s, len);
	conf.row[at].chars[len] = '\0';

	conf.row[at].rsize = 0;	
	conf.row[at].render = NULL;
	editorRowUpdate(&conf.row[at]);
	conf.numrows++;
	conf.dirty_flag++; 
}
/* Function for freeing memory held by editor_row that we are deleting */
void editorFreeRow(editor_row *row)
{
	free(row->render);
	free(row->chars);
}
/* Deleting a single element from an array of elements by it's index */
void editorDeleteRow(int at)
{
	if(at < 0 || at >= conf.numrows)
	{
		return;
	}
	editorFreeRow(&conf.row[at]);
	memmove(&conf.row[at], &conf.row[at + 1], sizeof(editor_row) * (conf.numrows - at - 1)); /* Overwriting the deleted row */
	conf.numrows--;
	conf.dirty_flag++;
}

void editorRowInsertChar(editor_row *row, int at, int c)
{
	if(at < 0 || at > row->size)
	{
		at = row->size;
	}
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorRowUpdate(row);
	conf.dirty_flag++;
}

/* Function for appending a string to the end of the row */
void editorRowAppendString(editor_row *row, char *s, size_t len)
{
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
  	row->size += len;
  	row->chars[row->size] = '\0';
  	editorRowUpdate(row);
  	conf.dirty_flag++;
}
void editorRowDeleteChar(editor_row *row, int at)
{
	if(at < 0 || at >= row->size)
	{
		return; 
	}
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorRowUpdate(row);
	conf.dirty_flag++;	
}

/* ====== FILE INPUT/OUTPUT ======*/
char *rowsToString(int *buflen)
{
	int totlen = 0; 
	int j; 
	for(j = 0; j < conf.numrows; j++)
		totlen += conf.row[j].size + 1;
	*buflen = totlen;
	char *buf = malloc(totlen);
	char *p = buf; 
	for(j = 0; j < conf.numrows; j++)
	{
		memcpy(p, conf.row[j].chars, conf.row[j].size);
		p += conf.row[j].size;
		*p  = '\n';
		p++;
	}
	return buf; 
}

/* Function for opening and reading given files */
void editorOpen(char *filename)
{
	free(conf.filename);
	conf.filename = strdup(filename);

	FILE *file = fopen(filename, "r"); /* Opening given file */
	if(file == NULL)
	{
		errorHandling("fopen");
	}
	char *line = NULL; /* NULL is passed to line variable so it allocates brand new memory for every next line it reads */
	size_t linecap = 0; 
	ssize_t linelen;
	/* Using while loops and conditions we can output multiple lines of text from user's file */
	while ((linelen = getline(&line, &linecap, file)) != -1) {
	    while (linelen > 0 && (line[linelen - 1] == '\n' ||
        	                   line[linelen - 1] == '\r'))
      		linelen--;
    		editorInsertRow(conf.numrows, line, linelen);
	
	}
  	free(line);
  	fclose(file);
  	conf.dirty_flag = 0;
}

/* Saving changes the user made*/
void saveChanges()
{
	/* If user opens a new file conf.filename will be NULL, we will output a prompt to user if that happens */
	if(conf.filename == NULL)
	{
		conf.filename = editorPrompt("Save file as(ESC = cancel): %s");
		/* If user pressed ESC, we output that save command was aborted*/
		if(conf.filename == NULL)
		{
			statusMessage("Save canceled.");
			return;
		}
	}
	int len; 
	char *buf = rowsToString(&len);
	int fd = open(conf.filename, O_RDWR | O_CREAT, 0644); /* Opening file for reading and writing with standard permissions*/
	if(fd != -1)
	{
		if(ftruncate(fd, len) != -1)
		{
			if(write(fd, buf, len) == len)
			{
				close(fd); /* Closing the file*/
				free(buf); /* Freeing memory*/
				conf.dirty_flag = 0;
				statusMessage("Changes written to disk(%d bytes)", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	statusMessage("Couldn't save changes to disk. Error: %s", strerror(errno));
}

/* ====== BUFFER APPEND ======*/
struct abuf
{
	char *b;
	int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);

	if(new == NULL)
	{
		return;
	}
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab)
{
	free(ab->b);
}

/* ====== EDITOR OPERATIONS ======*/
void editorInsertChar(int c)
{
	if(conf.cy  == conf.numrows)
	{
		editorInsertRow(conf.numrows, "", 0);
	}
	editorRowInsertChar(&conf.row[conf.cy], conf.cx, c);
	conf.cx++;
}

void editorNewLine()
{
	if(conf.cx == 0)
	{
		editorInsertRow(conf.cy, "", 0);
	}else
	{
		editor_row *row = &conf.row[conf.cy];
		editorInsertRow(conf.cy + 1, &row->chars[conf.cx], row->size - conf.cx);
		row = &conf.row[conf.cy];
		row->size = conf.cx;
		row->chars[row->size] = '\0';
		editorRowUpdate(row);
	}
	conf.cy++;
	conf.cx = 0;
}

void editorDelChar()
{
	if (conf.cy == conf.numrows) 
	{
		return;
	}
	/* If the cursor is in the beginning of a line, we return immediately*/
	if(conf.cx == 0 && conf.cy == 0)
	{
		return;
	}
	editor_row *row = &conf.row[conf.cy];
  	if (conf.cx > 0) {
    		editorRowDeleteChar(row, conf.cx - 1);
		conf.cx--;
  	}else
	{
		conf.cx = conf.row[conf.cy - 1].size;
		editorRowAppendString(&conf.row[conf.cy - 1], row->chars, row->size);
		editorDeleteRow(conf.cy);
		conf.cy--;
	}
}

/* ====== OUTPUT ======*/
void editorScroll()
{
	conf.rx = 0;
	if(conf.cy < conf.numrows)
	{
		conf.rx = editorRowCxToRx(&conf.row[conf.cy], conf.cx);
	}
	
	/* Checking if the user cursor is above the visible window, and if it is it scrolls up to that position*/
	if(conf.cy < conf.rowoff)
	{
		conf.rowoff = conf.cy;
	}

	/* Checking if the user cursor is below visible window*/
	if(conf.cy >= conf.rowoff + conf.screenrows)
	{
		conf.rowoff = conf.cy - conf.screenrows + 1;
	}
	if(conf.rx < conf.coloff)
	{
		conf.coloff = conf.rx; 
	}
	if(conf.rx >= conf.coloff + conf.screencols)
	{
		conf.coloff = conf.rx - conf.screencols + 1;
	}
}

/*Function for drawing ~ on every row(just like vim heh)*/
void editorRowDraw(struct abuf *ab)
{
	int y; 
	for(y = 0; y < conf.screenrows; y++)
	{
		int filerow = y + conf.rowoff;
		if(filerow >= conf.numrows){
			if(y >= conf.numrows)
			{
				/* Displaying our welcome message only if the text buffer is empty*/
				/* We don't want to display welcome message if the user opened his file*/
				if(conf.numrows == 0 && y == conf.screenrows / 3)
				{
					char welcome_message[80];
					int messagelen = snprintf(welcome_message, sizeof(welcome_message),
					  "Simplr editor - %s", SIMPLR_VERSION);
					if(messagelen > conf.screencols)
					{
						messagelen = conf.screencols;
					}
					int padding = (conf.screencols - messagelen) / 2;
					if(padding)
					{
						abAppend(ab, "-", 1);
						padding--;
					}
					while(padding--)
					{
						abAppend(ab, " ", 1);
					}
					abAppend(ab, welcome_message, messagelen);
	
				}else
				{
					abAppend(ab, "-", 1);
				}
			}
			}else
			{
				int len = conf.row[filerow].rsize - conf.coloff; 
				if(len < 0)
				{
					len = 0; 
				}
				if(len > conf.screencols)
				{
					len = conf.screencols;
				}
				abAppend(ab, &conf.row[filerow].render[conf.coloff], len);
		}
		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

/* Function for drawing the status bar on bottom of the screen*/
void statusBar(struct abuf *ab)
{
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
			conf.filename ? conf.filename : "[No Name]", conf.numrows,
			conf.dirty_flag ? "(file is changed)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
			conf.cy + 1, conf.numrows);
	if(len > conf.screencols)
	{
		len = conf.screencols;     
	}	
	abAppend(ab, status, len);
	while(len  < conf.screencols)
	{
		if(conf.screencols - len == rlen)
		{
			abAppend(ab, rstatus, rlen);
			break;
		}else
		{
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void messageBar(struct abuf *ab)
{
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(conf.status_message);
	if(msglen > conf.screencols)
	{
		msglen = conf.screencols;
	}
	if(msglen && time(NULL) - conf.status_message_time < 5)
	{
		abAppend(ab, conf.status_message, msglen);
	}
}

/*Function for clearing user's screen*/
void clearScreen() 
{
	editorScroll();
	struct abuf ab = ABUF_INIT;
	
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorRowDraw(&ab);
	statusBar(&ab);
	messageBar(&ab);
	
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (conf.cy - conf.rowoff) + 1, conf.cx + 1);
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", conf.cy + 1, conf.cx + 1);
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (conf.cy - conf.rowoff) + 1,
                                            (conf.rx - conf.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));	
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void statusMessage(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(conf.status_message, sizeof(conf.status_message), fmt, ap);
	va_end(ap);
	conf.status_message_time = time(NULL);
	
}
/* ====== INPUT ====== */
/* Function for prompting user for a text file in the status bar */
char *editorPrompt(char *prompt)
{
	size_t bufsize = 128;
	char *buf = malloc(bufsize); /* Allocating bufsize and returning a pointer to it*/
	size_t buflen = 0;
	buf[0] = '\0';

	while(1)
	{
		statusMessage(prompt, buf);
		clearScreen();
		int c = editorReadKey();
		if (c == DEL || c == CTRL_KEY('h') || c == BACKSPACE)
		{
		      	if (buflen != 0) buf[--buflen] = '\0';
   		}else if (c == '\x1b') {
   			statusMessage("");
  			free(buf);
     			return NULL;
    		}else if (c == '\r')
	        {
      			if (buflen != 0) {
        		statusMessage("");
       			return buf;
     		}
    		}else if (!iscntrl(c) && c < 128) 
		{
      			if (buflen == bufsize - 1)
		       	{
        			bufsize *= 2;
        			buf = realloc(buf, bufsize);
      			}
      			buf[buflen++] = c;
      			buf[buflen] = '\0';
    		}
  	}
    }
/*Function for moving user's cursor in the editor*/
void cursorMove(int key)
{
	editor_row *row = (conf.cy >= conf.numrows) ? NULL : &conf.row[conf.cy];
	
	switch(key)
	{
		/*We are also making sure the cursor doesn't go off screen by using if statements and screen collumns&rows*/
		case LEFT:
			if(conf.cx != 0)
			{
				conf.cx--;
			}else if(conf.cy > 0) /* If the user presses left arrow key, it will jump to end of the text line*/
			{
				conf.cy--;
				conf.cx = conf.row[conf.cy].size;
			}
			break;
		case RIGHT:
			/* Now the cursor cannot go past the text line */
			if(row && conf.cx < row->size)
			{
				conf.cx++;
			}else if(row && conf.cx == row->size) /* If the user presses right arrow key, it will jump to the beginning of the text line*/
			{
				conf.cy++;
				conf.cx = 0;
			}
			break;
		case UP:
			if(conf.cy != 0)
			{
				conf.cy--;
			}
			break; 
		case DOWN:
			if(conf.cy < conf.numrows)
			{
				conf.cy++;
			}
			break;
	}
	/* Now the cursor will just snap to the end of the text line */
	row = (conf.cy >= conf.numrows) ? NULL : &conf.row[conf.cy];
	int rowlen = row ? row->size : 0;
	if(conf.cx > rowlen)
	{
		conf.cx = rowlen;
	}
}

/*Function for listening for a keypress and handling it right*/
void editorProcessKeypress()
{
	static int quit_times = SIMPLR_QUIT_TIMES;
	int c = editorReadKey();
	switch(c)
	{
		case '\r':
			editorNewLine();
			break;
	    	case CTRL_KEY('q'):
      			if (conf.dirty_flag && quit_times > 0) {
			        statusMessage("WARNING! File has unsaved changes. "
          				"Press CTRL + Q %d more times to quit.", quit_times);
			        quit_times--;
			        return;
			}
	  	        write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		case CTRL_KEY('s'):
			saveChanges();
			break; 
			
		/* If home key is pressed, cursor moves to beginning */
		case HOME:
			conf.cx = 0;
			break; 
		/* If end key is pressed, cursor moves to the end of the text line*/			
		case END:
			if(conf.cy < conf.numrows)
			{
				conf.cx = conf.row[conf.cy].size; 
			}	
			break;
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL:
			if(c == DEL)
			{
				cursorMove(RIGHT);
			}
			editorDelChar();
			break;
		case PAGE_UP:
    		case PAGE_DOWN:
      			{
				/* Adding page up and page down scroll feature*/
      				if(c == PAGE_UP)
				{
					conf.cy = conf.rowoff;
				}else if(c == PAGE_DOWN)
				{
					conf.cy = conf.rowoff + conf.screenrows - 1;
					if(conf.cy > conf.numrows)
					{
						conf.cy = conf.numrows;
					}
				}
				int times = conf.screenrows; 
				while(times--)
				{
					cursorMove(c == PAGE_UP ? UP : DOWN);
				}
			}
      			break;

		case UP:
		case DOWN:
		case LEFT:
		case RIGHT:
			cursorMove(c);
			break;
			
		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editorInsertChar(c);
			break;

	}
	quit_times = SIMPLR_QUIT_TIMES;
}

/* ====== INITIALIZATION ======*/
/*Function to initialize all the fields in conf structure*/
void initEditor()
{
	conf.cx = 0;
	conf.cy = 0;
	conf.numrows = 0;
	conf.row = NULL;
	conf.dirty_flag = 0; 
	conf.rowoff = 0; /* We initialize it as 0 which means user will be scrolled to the top of the file by default*/
	conf.coloff = 0;
	conf.filename = NULL;
	conf.status_message[0] = '\0';
	conf.status_message_time = 0;
	if(getWindowSize(&conf.screenrows, &conf.screencols) == -1)
	{
		errorHandling("getWindowSize");
	}
	conf.screenrows -= 2; /* screenrows is decremented so that the editor doesn't draw anything on the bottom*/
}

int main(int argc, char *argv[])
{
	enableRawMode();
	initEditor();
	/* If user input passes this argument, the given file is open and read */
	if(argc >= 2)
	{
		editorOpen(argv[1]); /* Calling function for opening and reading given file */
	}
	
	statusMessage("Commands: CTRL + S = save | CTRL + Q = exit");
	
	while(1)
	{
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
		clearScreen();
		editorProcessKeypress();

	}
	return 0; 
}
