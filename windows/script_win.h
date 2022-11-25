#ifndef SCRIPT_WIN_H
#define SCRIPT_WIN_H SCRIPT_WIN_H

#define script_line_size 4096
#define script_cond_size 256

struct scriptDATA {
   int line_delay;  /* ms */
   int char_delay;  /* ms */
   char cond_char;  /* condition/remark start character */
   char cond_charR;  /* copy for recording */
   int cond_use;  /* use condition from file */
   int enable;  /* wait for host response */
   int except;  /* except firstline */
   int timeout;  /* sec */
   int crlf;  /* cr/lf translation */
   char waitfor[script_cond_size];
   int waitfor_c;
   char halton[script_cond_size];
   int halton_c;

   char waitfor2[script_cond_size];
   int waitfor2_c;
   int runs;
   int send;

   char * filebuffer;
   char * nextnextline;
   char * filebuffer_end;
   long latest;

   FILE *scriptrecord;

   char * nextline;
   int nextline_c;
   int nextline_cc;
   char remotedata[script_line_size];
   int remotedata_c;
   char localdata[script_line_size];
   int localdata_c;
   
   WinGuiSeat *wgs;
};
typedef struct scriptDATA ScriptData;


/* script cr/lf translation */
enum {SCRIPT_OFF, SCRIPT_NOLF, SCRIPT_CR, SCRIPT_REC};

void script_init(ScriptData * scriptdata, WinGuiSeat *wgs);
BOOL script_sendfile(ScriptData * scriptdata, Filename * script_filename);
void script_close(ScriptData * scriptdata);

int script_chkline(ScriptData * scriptdata);
void script_timeout(void *ctx, long unsigned int now);
void script_sendline(void *ctx, long unsigned int now);
void script_sendchar(void *ctx, long unsigned int now);
void script_getline(ScriptData * scriptdata);
int script_findline(ScriptData * scriptdata);
void script_cond_set(char * cond, int *p, char *in, int sz);


int prompt_scriptfile(HWND hwnd, char * filename);
void script_menu(ScriptData * scriptdata);

#endif
