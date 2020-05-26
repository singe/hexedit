/* hexedit -- Hexadecimal Editor for Binary Files
   Copyright (C) 1998 Pixel (Pascal Rigaux)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.*/
#include "hexedit.h"


static void goto_char(void);
static void goto_sector(void);
static void save_buffer(void);
static void escaped_command(void);
static void help(void);
static void short_help(void);


/*******************************************************************************/
/* interactive functions */
/*******************************************************************************/

static void forward_char(void)
{
  if (!hexOrAscii || cursorOffset)
    move_cursor(+1);
  if (hexOrAscii) cursorOffset = (cursorOffset + 1) % 2;
}

static void backward_char(void)
{
  if (!hexOrAscii || !cursorOffset)
    move_cursor(-1);
  if (hexOrAscii) cursorOffset = (cursorOffset + 1) % 2;
}

static void next_line(void)
{
  move_cursor(+lineLength);
}

static void previous_line(void)
{
  move_cursor(-lineLength);
}

static void forward_chars(void)
{
  move_cursor(+blocSize);
}

static void backward_chars(void)
{
  move_cursor(-blocSize);
}

static void next_lines(void)
{
  move_cursor(+lineLength * blocSize);
}

static void previous_lines(void)
{
  move_cursor(-lineLength * blocSize);
}

static void beginning_of_line(void)
{
  cursorOffset = 0;
  move_cursor(-(cursor % lineLength));
}

static void end_of_line(void)
{
  cursorOffset = 0;
  if (!move_cursor(lineLength - 1 - cursor % lineLength))
    move_cursor(nbBytes - cursor);
}

static void scroll_up(void)
{
  move_base(+page);

  if (mark_set)
    updateMarked();
}

static void scroll_down(void)
{
  move_base(-page);

  if (mark_set)
    updateMarked();
}

static void beginning_of_buffer(void)
{
  cursorOffset = 0;
  set_cursor(0);
}

static void end_of_buffer(void)
{
  INT s = getfilesize();
  cursorOffset = 0;
  if (mode == bySector) set_base(myfloor(s, page));
  set_cursor(s);
}

static void suspend(void) { kill(getpid(), SIGTSTP); }
static void undo(void) { discardEdited(); readFile(); }
static void quoted_insert(void) { setTo(getch()); }
static void toggle(void) { hexOrAscii = (hexOrAscii + 1) % 2; }

static void recenter(void)
{
  if (cursor) {
    base = base + cursor;
    cursor = 0;
    readFile();
  }
}

static void find_file(void)
{
  if (!ask_about_save_and_redisplay()) return;
  if (!findFile()) { displayMessageAndWaitForKey("No such file or directory"); return; }
  openFile();
  readFile();
}

static void redisplay(void) { clear(); }

static void delete_backward_char(void)
{
  backward_char();
  removeFromEdited(base + cursor, 1);
  readFile();
  cursorOffset = 0;
  if (!tryloc(base + cursor)) end_of_buffer();
}

static void delete_backward_chars(void)
{
  backward_chars();
  removeFromEdited(base + cursor, blocSize);
  readFile();
  cursorOffset = 0;
  if (!tryloc(base + cursor)) end_of_buffer();
}

static void truncate_file(void)
{
  displayOneLineMessage("Really truncate here? (y/N)");
  if (tolower(getch()) == 'y') {
    if (biggestLoc > base+cursor && ftruncate(fd, base+cursor) == -1)
      displayMessageAndWaitForKey(strerror(errno));
    else {
      removeFromEdited(base+cursor, lastEditedLoc - (base+cursor));
      if (mark_set) {
	if (mark_min >= base + cursor || mark_max >= base + cursor)
	  unmarkAll();
      }
      if (biggestLoc > base+cursor)
	biggestLoc = base+cursor;
      readFile();
    }
  }
}

static void add_note(void)
{
  // Grow the notes buffer if we need to
  if (base+cursor > notes_size) {
    notes_size = base+cursor+NOTE_SIZE;
    notes = (noteStruct*) realloc(notes,notes_size*sizeof(noteStruct));
  }

  char tmp[BLOCK_SEARCH_SIZE], msg[BLOCK_SEARCH_SIZE];

  // Check if we're adding a new note or changing a existing note
  if (notes[base+cursor].note) {
    // Handle the first change of a note set via tag file
    if (!lastNote) lastNote = (char*) malloc(NOTE_SIZE);
    snprintf(lastNote, NOTE_SIZE, "%s", notes[base+cursor].note);
    snprintf(msg, BLOCK_SEARCH_SIZE, "Change the note for 0x%llx: ", base+cursor);
  }
   else
  {
    snprintf(msg, BLOCK_SEARCH_SIZE, "Enter a note for 0x%llx: ", base+cursor);
    free(notes[base+cursor].note);
    notes[base+cursor].note = NULL;
    notes[base+cursor].note = (char*) malloc(NOTE_SIZE);
  }
  char** last = &lastNote; 

  //if (!displayMessageAndGetString(msg, last, tmp, sizeof(tmp))) return;
  if (!displayMessageAndGetString(msg, last, tmp, sizeof(tmp))) return;

  // You don't actually see this, but it clears artifacts left on the screen
  displayOneLineMessage("Note Set");

  // Overly paranoid, but clear the note before copying it in
  memset(notes[base+cursor].note,'\0',NOTE_SIZE);
  snprintf(notes[base+cursor].note,NOTE_SIZE,"%s",tmp);
}

static void get_note(void)
{
  if (notes[base+cursor].note)
    displayMessageAndWaitForKey(notes[base+cursor].note);
  else
    displayMessageAndWaitForKey("No note set for current position!");
}

static void delete_note(void)
{
  if (notes[base+cursor].note) {
    free(notes[base+cursor].note);
    notes[base+cursor].note = NULL;
    displayMessageAndWaitForKey("Note deleted");
  }
}

static void change_color(void)
{
  //CYAN MAGENTA YELLOW
  displayOneLineMessage("Tag this byte with which color (1/2/3)?");
  int color=0;
  switch (getch())
  {
    case '1': color = (int) COLOR_PAIR(5); break;
    case '2': color = (int) COLOR_PAIR(6); break;
    case '3': color = (int) COLOR_PAIR(7); break;
    default: displayMessageAndWaitForKey("Invalid choice, must be between 1-3"); return;
  }
  // Are we changing a range or a single byte?
  if (mark_set)
    for (int i = MAX(mark_min - base, 0); i <= MIN(mark_max - base, nbBytes - 1); i++)
    {
      // It's late and I'm too tired to work out how to nix the color but
      // preserve the other bits
      int m = bufferAttr[i] & MARKED;
      int b = bufferAttr[i] & A_BOLD;
      bufferAttr[i] = color;
      bufferAttr[i] |= TAGGED;
      bufferAttr[i] |= m;
      bufferAttr[i] |= b;
    }
  else
  {
    // It's late and I'm too tired to work out how to nix the color but
    // preserve the other bits
    int m = bufferAttr[base+cursor] & MARKED;
    int b = bufferAttr[base+cursor] & A_BOLD;
    bufferAttr[base+cursor] = color;
    bufferAttr[base+cursor] |= TAGGED;
    bufferAttr[base+cursor] |= m;
    bufferAttr[base+cursor] |= b;
  }
}

static void firstTimeHelp(void)
{
  static int firstTime = TRUE;

  if (firstTime) {
    firstTime = FALSE;
    short_help();
  }
}

static void set_mark_command(void)
{
  unmarkAll();
  if ((mark_set = not(mark_set))) {
    markIt(cursor);
    mark_min = mark_max = base + cursor;
  }
}


int setTo(int c)
{
  int val;

  if (cursor > nbBytes) return FALSE;
  if (hexOrAscii) {
      if (!isxdigit(c)) return FALSE;
      val = hexCharToInt(c);	  
      val = cursorOffset ? setLowBits(buffer[cursor], val) : setHighBits(buffer[cursor], val);
  }
  else val = c;

  if (isReadOnly) {
    displayMessageAndWaitForKey("File is read-only!");
  } else {
    setToChar(cursor, val);
    forward_char();
  }
  return TRUE;
}


/****************************************************
 ask_about_* or functions that present a prompt
****************************************************/


int ask_about_save(void)
{
  if (edited) {
    displayOneLineMessage("Save changes (Yes/No/Cancel) ?");

    switch (tolower(getch()))
      {
      case 'y': save_buffer(); break;
      case 'n': discardEdited(); break;

      default:
	return FALSE;
      }
    return TRUE;
  }
  return -TRUE;
}

int ask_about_save_and_redisplay(void)
{
  int b = ask_about_save();
  if (b == TRUE) {
    readFile();
    display();
  }
  return b;
}

void ask_about_save_and_quit(void)
{
  if (ask_about_save()) quit();
}

static void goto_char(void)
{
  INT i;

  displayOneLineMessage("New position ? ");
  ungetstr("0x");
  if (!get_number(&i) || !set_cursor(i)) displayMessageAndWaitForKey("Invalid position!");
}

static void goto_sector(void)
{
  INT i;

  displayOneLineMessage("New sector ? ");
  if (get_number(&i) && set_base(i * SECTOR_SIZE))
    set_cursor(i * SECTOR_SIZE);
  else
    displayMessageAndWaitForKey("Invalid sector!");
}



static void save_buffer(void)
{
  int displayedmessage = FALSE;
  typePage *p, *q;
  for (p = edited; p; p = q) {
    if (LSEEK_(fd, p->base) == -1 || write(fd, p->vals, p->size) == -1)
      if (!displayedmessage) {  /* It would be annoying to display lots of error messages when we can't write. */
	displayMessageAndWaitForKey(strerror(errno));
	displayedmessage = TRUE;
      }
    q = p->next;
    freePage(p);
  } 
  edited = NULL;
  if (lastEditedLoc > fileSize) fileSize = lastEditedLoc;
  lastEditedLoc = 0;
  //memset(bufferAttr, A_NORMAL, page * sizeof(*bufferAttr));
  for (int i=base; i<nbBytes; i++)
    bufferAttr[i] &= ~MODIFIED;
  if (displayedmessage) {
    displayMessageAndWaitForKey("Unwritten changes have been discarded");
    readFile();
    if (cursor > nbBytes) set_cursor(getfilesize());
  }
  if (mark_set) markSelectedRegion();
}

static void help(void)
{
  char *args[3];
  int status;

  args[0] = "man";
  args[1] = "hexedit";
  args[2] = NULL;
  endwin();
  if (fork() == 0) {
    execvp(args[0], args);
    exit(1);
  }
  wait(&status);
  refresh();
  raw();
}

static void short_help(void)
{
  displayMessageAndWaitForKey("Unknown command, press F1 for help");
}



/*******************************************************************************/
/* key_to_function */
/*******************************************************************************/
int key_to_function(int key)
{
  oldcursor = cursor;
  oldcursorOffset = cursorOffset;
  oldbase = base;
  /*printf("*******%d******\n", key);*/

  switch (key)
    {
    case ERR:
    case KEY_RESIZE:
      /*no-op*/
      break;

    case KEY_RIGHT:
    case CTRL('F'):
      forward_char();
      break;

    case KEY_LEFT:
    case CTRL('B'):
      backward_char();
      break;

    case KEY_DOWN:
    case CTRL('N'):
      next_line();
      break;

    case KEY_UP:
    case CTRL('P'):
      previous_line();
      break;

    case ALT('F'):
      forward_chars();
      break;

    case ALT('B'):
      backward_chars();
      break;

    case ALT('N'):
      next_lines();
      break;

    case ALT('P'):
      previous_lines();
      break;

    case CTRL('A'):
    case KEY_HOME:
      beginning_of_line();
      break;

    case CTRL('E'):
    case KEY_END:
      end_of_line();
      break;

    case KEY_NPAGE:
    case CTRL('V'):
    case KEY_F(6):
      scroll_up();
      break;

    case KEY_PPAGE:
    case ALT('V'):
    case KEY_F(5):
      scroll_down();
      break;

    case '<':
    case ALT('<'):
      beginning_of_buffer();
      break;

    case '>':
    case ALT('>'):
      end_of_buffer();
      break;

    case KEY_SUSPEND:
    case CTRL('Z'):
      suspend();
      break;

    case CTRL('U'):
    case CTRL('_'):
      undo();
      break;

    case CTRL('Q'):
      quoted_insert();
      break;

    case CTRL('T'):
    case '\t':
      toggle();
      break;

    case '/':
    case CTRL('S'):
      search_forward();
      break;

    case CTRL('R'):
      search_backward();
      break;

    case CTRL('G'):
    case KEY_F(4):
      goto_char();
      break;

    case ALT('L'):
      recenter();
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      if (mode == bySector) goto_sector(); else goto_char();
      break;

    case CTRL('W'):
    case KEY_F(2):
      save_buffer();
      break;

    case CTRL('['): /* escape */
      escaped_command();
      break;

    case KEY_F(1):
    case ALT('H'):
      help();
      break;

    case KEY_F(3):
    case CTRL('O'):
      find_file();
      break;

    case CTRL('L'):
      redisplay();
      break;

    case CTRL('H'):
    case KEY_BACKSPACE:
      delete_backward_char();
      break;

    case CTRL('H') | 0x80: /* CTRL-ALT-H */
      delete_backward_chars();
      break;

    case CTRL(' '):
    case KEY_F(9):
      set_mark_command();
      break;

    case CTRL('D'):
    case ALT('W'):
    case KEY_DC:
    case KEY_F(7):
    case 0x7F: /* found on a sun */
      copy_region();
      break;

    case CTRL('Y'):
    case KEY_IC:
    case KEY_F(8):
      yank();
      break;

    case ALT('Y'):
    case KEY_F(11):
      yank_to_a_file();
      break;

    case KEY_F(12):
    case ALT('I'):
      fill_with_string();
      break;

    case CTRL('C'):
      quit();
      break;

    case ALT('T'):
      truncate_file();
      break;

    case KEY_F(0):
    case KEY_F(10):
    case CTRL('X'):
      ask_about_save_and_quit();
      break;

    default:
      if ((key >= 256 || !setTo(key))) firstTimeHelp();
    }

  return TRUE;
}



static void escaped_command(void) 
{
  char tmp[BLOCK_SEARCH_SIZE];
  int c, i;

  c = getch();
  switch (c)
  {
  case KEY_RIGHT:
  case 'f': 
    forward_chars();
    break;
    
  case KEY_LEFT:
  case 'b':
    backward_chars();
    break;

  case KEY_DOWN:
  case 'n':
    next_lines();
    break;

  case KEY_UP:
  case 'p':
    previous_lines();
    break;

  case 'v':
    scroll_down();
    break;

  case KEY_HOME:
  case '<':
    beginning_of_buffer();
    break;

  case KEY_END:
  case '>':
    end_of_buffer();
    break;

  case 'l':
    recenter();
    break;

  case 'h':
    help();
    break;

  case CTRL('H'):
    delete_backward_chars();
    break;

  case 'w':
    copy_region();
    break;

  case 'y':
    yank_to_a_file();
    break;

  case 'i':
    fill_with_string();
    break;

  case 't':
    truncate_file();
    break;

  case 'o':
    add_note();
    break;

  case 'g':
    get_note();
    break;

  case 'd':
    delete_note();
    break;

  case 'c':
    change_color();
    break;

  case 's':
    writeTagFile();
    break;

  case '':
    c = getch();
    if (c == 'O') {
      switch (c = getch())
      {
      case 'C': 
	forward_chars();
	break;
    
      case 'D':
	backward_chars();
	break;

      case 'B':
	next_lines();
	break;

      case 'A':
	previous_lines();
	break;

      case 'H':
	beginning_of_buffer();
	break;

      case 'F':
	end_of_buffer();
	break;

      case 'P': /* F1 on a xterm */
	help();
	break;

      case 'Q': /* F2 on a xterm */
	save_buffer();
	break;

      case 'R': /* F3 on a xterm */
	find_file();
	break;

      case 'S': /* F4 on a xterm */
	goto_char();
	break;

      default: 
	firstTimeHelp();
      }
    } else firstTimeHelp();
    break;

  case '[': 
    for (i = 0; i < BLOCK_SEARCH_SIZE - 1; i++) { tmp[i] = c = getch(); if (!isdigit(c)) break; }
    tmp[i + 1] = '\0';
    
    if (0);
    else if (streq(tmp, "2~")) yank();
    else if (streq(tmp, "5~")) scroll_down();
    else if (streq(tmp, "6~")) scroll_up();
    else if (streq(tmp, "7~")) beginning_of_buffer();
    else if (streq(tmp, "8~")) end_of_buffer();
    else if (streq(tmp, "010q" /* F10 on a sgi's winterm */)) ask_about_save_and_quit();
    else if (streq(tmp, "193z")) fill_with_string();
    else if (streq(tmp, "214z")) beginning_of_line();
    else if (streq(tmp, "216z")) scroll_down();
    else if (streq(tmp, "220z")) end_of_line();
    else if (streq(tmp, "222z")) scroll_up();
    else if (streq(tmp, "233z")) ask_about_save_and_quit();
    else if (streq(tmp, "234z" /* F11 on a sun */)) yank_to_a_file();
    else if (streq(tmp, "247z")) yank();
    else if (streq(tmp, "11~" /* F1 on a rxvt */)) help();
    else if (streq(tmp, "12~" /* F2 on a rxvt */)) save_buffer();
    else if (streq(tmp, "13~" /* F3 on a rxvt */)) find_file();
    else if (streq(tmp, "14~" /* F4 on a rxvt */)) goto_char();
    else if (streq(tmp, "15~" /* F5 on a rxvt */)) scroll_down();
    else if (streq(tmp, "17~" /* F6 on a rxvt */)) scroll_up();
    else if (streq(tmp, "18~" /* F7 on a rxvt */)) copy_region();
    else if (streq(tmp, "19~" /* F8 on a rxvt */)) yank();
    else if (streq(tmp, "20~" /* F9 on a rxvt */)) set_mark_command();
    else if (streq(tmp, "21~" /* F10 on a rxvt */)) ask_about_save_and_quit();
    else if (streq(tmp, "23~" /* F11 on a rxvt */)) yank_to_a_file();
    else if (streq(tmp, "24~" /* F12 on a rxvt */)) fill_with_string();
    else firstTimeHelp();
    break;

  default:
    firstTimeHelp();
  }
}
