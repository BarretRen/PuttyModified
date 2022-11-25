/* sort of copy of prompt_keyfile in winpgen.c
*/
int prompt_scriptfile(HWND hwnd, char * filename)
{
    OPENFILENAME of;
    memset(&of, 0, sizeof(of));
    of.hwndOwner = hwnd;
    of.lpstrFilter = "All Files (*.*)\0*\0\0\0";
    of.lpstrCustomFilter = NULL;
    of.nFilterIndex = 1;
    of.lpstrFile = filename;
    *filename = '\0';
    of.nMaxFile = FILENAME_MAX;
    of.lpstrFileTitle = NULL;
    of.lpstrTitle = "Select Script File ";
    of.Flags = 0;
    return request_file(NULL, &of, FALSE, 1);
}

/* change script entry in the menu
*/
void script_menu(ScriptData * scriptdata)
{
  int i;
  for (i = 0; i < lenof(scriptdata->wgs->popup_menus); i++)
  {
    if(scriptdata->runs)
    {
      ModifyMenu(scriptdata->wgs->popup_menus[i].menu, IDM_SCRIPTSEND, MF_BYCOMMAND | MF_ENABLED, IDM_SCRIPTHALT, "Stop sending script");
    }
    else
    {
      ModifyMenu(scriptdata->wgs->popup_menus[i].menu, IDM_SCRIPTHALT, MF_BYCOMMAND | MF_ENABLED, IDM_SCRIPTSEND, "Send script file");
    }
  }
}

/*  close script file
 */
void script_close(ScriptData * scriptdata)
{
    scriptdata->runs=FALSE;

    expire_timer_context(scriptdata);  /* seems it dusn't work in windows */
    scriptdata->latest = 0;  /* so block previous timeouts this way */

    script_menu(scriptdata);

    if(scriptdata->filebuffer!=NULL)
    {
      sfree(scriptdata->filebuffer);
      scriptdata->filebuffer = NULL;
    }
}

void script_init(ScriptData * scriptdata, WinGuiSeat *wgs)
{
    scriptdata->line_delay = 0;
    if(scriptdata->line_delay<5)
      scriptdata->line_delay = 5;
    scriptdata->line_delay = scriptdata->line_delay * TICKSPERSEC / 1000;
    scriptdata->char_delay = 1 * TICKSPERSEC / 1000;
    if(scriptdata->cond_char =='\0') 
      scriptdata->cond_char =':';  /* if none use default */
    scriptdata->enable = 0;
    scriptdata->cond_use = FALSE;
    scriptdata->except = 0;
    scriptdata->timeout = 30 * TICKSPERSEC ;  /* in winstuff.h */

    scriptdata->crlf = SCRIPT_OFF;

    scriptdata->waitfor2[0] = '\0';
    scriptdata->waitfor2_c = -1;  /* -1= there is no condition from file, 0= there an empty line (cr/lf) */

    scriptdata->runs = FALSE;
    scriptdata->send = FALSE;
    scriptdata->filebuffer = NULL;
    scriptdata->wgs = wgs;

    scriptdata->latest = 0;

    scriptdata->remotedata_c = script_cond_size;
    scriptdata->remotedata[0] = '\0' ;

    scriptdata->localdata_c = 0 ;
    scriptdata->localdata[0] = '\0' ;
}

/* send script file to host
   assume script file short - read complete file in memory
   makes read ahead to detect lf lf possible
*/
BOOL script_sendfile(ScriptData * scriptdata, Filename * scriptfile)
{
    FILE * fp;
    long fsize;
    if(scriptdata->runs)
      return FALSE;  /* a script is already running */

    fp = f_open(scriptfile, "rb", FALSE);
    if(fp==NULL)
    {
      logevent(NULL, "script file not found");
        return FALSE;  /* scriptfile not found or something like it */
    }

    scriptdata->runs = TRUE;
    script_menu(scriptdata);

    fseek(fp, 0L, SEEK_END);
    fsize = ftell(fp);  /* script file size */
    fseek(fp, 0L, SEEK_SET);

    scriptdata->nextnextline = scriptdata->filebuffer = smalloc(fsize);
    scriptdata->filebuffer_end = &scriptdata->filebuffer[fsize];

    if(fread(scriptdata->filebuffer,sizeof(char),fsize,fp)!=fsize)
    {
      logevent(NULL, "script file read failed");
      fclose(fp);
      script_close(scriptdata);
      return FALSE;
    }

    fclose(fp);

    logevent(NULL, "sending script to host ...");

    script_getline(scriptdata);
    script_chkline(scriptdata);

    if(scriptdata->enable && !scriptdata->except)  /* start timeout if wait for prompt is enabled */
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else
    {
      scriptdata->send = TRUE;
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
    return TRUE;
}

/* send line, called by timer after linedelay
*/
void script_sendline(void *ctx, long unsigned int now)
{
    ScriptData *scriptdata = (ScriptData *) ctx;

    if(!scriptdata->runs)  /* script terminated */
      return;

    if(scriptdata->nextline_c==0) /* no more lines */
    {
      script_close(scriptdata);
      logevent(NULL, " ...finished sending script");
      return;
    }

    if(scriptdata->char_delay>1)
    {
      schedule_timer(scriptdata->char_delay, script_sendchar, scriptdata);
      return;
    }

    if(scriptdata->char_delay==0)
      ldisc_send(scriptdata->wgs->ldisc, scriptdata->nextline, scriptdata->nextline_c, 0);
    else
    {
      int i;
      for(i=0;i<scriptdata->nextline_c;i++)
        ldisc_send(scriptdata->wgs->ldisc, &scriptdata->nextline[i], 1, 0);
    }

    script_getline(scriptdata);
    script_chkline(scriptdata);

    if(scriptdata->enable)
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else
    {
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
    return;
}

/* send char, called by timer after char_delay
*/
void script_sendchar(void *ctx, long unsigned int now)
{
    ScriptData *scriptdata = (ScriptData *) ctx;

    if(!scriptdata->runs)  /* script terminated */
      return;

    if(scriptdata->nextline_c==0) /* no more lines */ /* can never happen?  it's captured by send line */
    {
      script_close(scriptdata);
      logevent(NULL, "....finished sending script");
      return;
    }

    /* send char */
    if(scriptdata->nextline_cc < scriptdata->nextline_c)
      ldisc_send(scriptdata->wgs->ldisc, &scriptdata->nextline[scriptdata->nextline_cc++], 1, 0);

   /* set timer for next */
   if(scriptdata->nextline_cc < scriptdata->nextline_c)
   {
     schedule_timer(scriptdata->char_delay, script_sendchar, scriptdata);
     return;
   }

   /* last char - get next line and set timer */
   script_getline(scriptdata);
   script_chkline(scriptdata);

   if(scriptdata->enable)
    {
      scriptdata->send = FALSE;
      scriptdata->latest = schedule_timer(scriptdata->timeout, script_timeout, scriptdata);
    }
    else
    {
      schedule_timer(scriptdata->line_delay, script_sendline, scriptdata);
    }
   return;
 }

/* read a line from script file
   skip comment/condition lines if conditions are disabled
   if conditions are enabled skip only comments
   e.g. if ':' is the condition marker:
   :condition, the prompt from host were we will wait for
   ::comment, a comment line
*/
void script_getline(ScriptData * scriptdata)
{
    int neof;
    int i;

    if(!scriptdata->runs || scriptdata->filebuffer==NULL)
      return;

    do {
      do
        neof=script_findline(scriptdata);
      while(neof && ( (!scriptdata->cond_use && scriptdata->nextline[0]==scriptdata->cond_char)
                      || ( scriptdata->cond_use && scriptdata->nextline[0]==scriptdata->cond_char
                           && scriptdata->nextline[1]==scriptdata->cond_char ) ) ) ;
      if(!neof)
      {
        scriptdata->nextline_c = 0;
        scriptdata->nextline_cc = 0;
        return;
      }
      
      i = scriptdata->nextline_c ;
      switch(scriptdata->crlf)  /* translate cr/lf */
      {
        case SCRIPT_OFF: /* no translation */
          break;

        case SCRIPT_NOLF:  /* remove LF (recorded file)*/
          if(scriptdata->nextline[i-1]=='\n')
            i--;
          break;

        case SCRIPT_CR: /* replace cr/lf by cr */
          if(scriptdata->nextline[i-1]=='\n')
            i--;
          if(i>0 && scriptdata->nextline[i-1]=='\r')
            i--;
          scriptdata->nextline[i++]='\r';
          break;

        default:
          break;
      }
    } while(i==0);  /* skip empty lines */
    scriptdata->nextline_c = i;
    scriptdata->nextline_cc = 0;
}

void script_cond_set(char * cond, int *p, char *in, int sz)
{
    int i = 0;
    (*p) = 0;

    while(sz>0 && (in[sz-1] =='\n' || in[sz-1] =='\r'))  /* remove cr/lf */
       sz--;

    if(sz==0)
    {
      cond[*p]='\0';
    }
    else if(in[0]!='"')
    {
      if(sz>(script_cond_size-1))
        i = sz - (script_cond_size-1);  /* line to large - use only last part */
      cond[(*p)++]='\0';
      while(i<sz)
        cond[(*p)++] = in[i++];
    }
    else
    {
      if(sz>script_cond_size)
        sz = script_cond_size;  /* word list to large, use only first part */
      i++;  //skip staring "
      while(i<sz)
      {
        cond[(*p)++] = '\0';
        while(i<sz && in[i]!='"')  //copy upto end or " 
          cond[(*p)++] = in[i++];
        i++;  
        while(i<sz && in[i]==' ')  //skip spaces after/between " 
          i++;
        while(i<sz && in[i]=='"')  //skip aditional "
          i++;
      }
    }
}

/* check line in nextline buffer
   if it's a condition copy/translate it to 'waitfor' and read the nextline
 */
int script_chkline(ScriptData * scriptdata)
{
    if(scriptdata->nextline_c>0 && scriptdata->nextline[0]==scriptdata->cond_char)
    {
      script_cond_set(scriptdata->waitfor2,&scriptdata->waitfor2_c,&scriptdata->nextline[1],scriptdata->nextline_c-1);
      script_getline(scriptdata);
      return TRUE;
    }
    else
    {
      scriptdata->waitfor2_c = -1;
      scriptdata->waitfor2[0] = '\0';
    }
    return FALSE;
}

/* called by timer after wait for prompt timeout
*/
void script_timeout(void *ctx, long unsigned int now)
{
    ScriptData * scriptdata = (ScriptData *) ctx;

    /* disable timer seems not to be working, timeout is disabled by keeping track of time */
    if(abs(now - scriptdata->latest)<50)
    {
      script_close(scriptdata);
      logevent(NULL, "script timeout !");
    }
}

/* find nextline in buffer
  */
int script_findline(ScriptData * scriptdata)
{
    if(scriptdata->filebuffer==NULL)
      return FALSE;

    if(scriptdata->nextnextline >= scriptdata->filebuffer_end)  /* end of filebuffer */
      return FALSE;

    scriptdata->nextline = scriptdata->nextnextline;
    scriptdata->nextline_c = 0;
    while(scriptdata->nextnextline < scriptdata->filebuffer_end && scriptdata->nextnextline[0] !='\n')
    {
      scriptdata->nextnextline++;
      scriptdata->nextline_c++;
    }

    if(scriptdata->nextnextline < scriptdata->filebuffer_end)
    {
      /* while loop ended due lf found, correct pointers to point to next nextline */
      scriptdata->nextnextline++;
      scriptdata->nextline_c++;

      /* cr, lf and crlf are recorded as crlf, lflf and crlflflf
        correct pointers so nextnextline points to the next nextline
        and nextline_c is the size of nextline
        in script file      real data
        data    lf          data
        data    lf lf       data lf     ** correct pointer +1
        data cr lf          data cr
        data cr lf lf lf    data cr lf  ** correct pointer +2
        data cr lf lf       data cr lf  ** this can't be recorded, user edit
        1:   -2 -1  0 +1
        2:   -3 -2 -1  0
      */
      if(scriptdata->crlf==SCRIPT_REC)
      {
        /* 1: not past end of buffer and char is a lf */
        if(scriptdata->nextnextline < scriptdata->filebuffer_end && scriptdata->nextnextline[0]=='\n')
        {
          scriptdata->nextnextline++;
          scriptdata->nextline_c++;

          /* 2: not before buffer start and char at position -3 is a cr
             also not past buffer end and char is a lf
          */
          if( &scriptdata->nextnextline[-3] >= scriptdata->filebuffer && scriptdata->nextnextline[-3]=='\r'
              && scriptdata->nextnextline < scriptdata->filebuffer_end && scriptdata->nextnextline[0]=='\n' )
            {
              scriptdata->nextnextline++;
              //scriptdata->nextline_c++;
            }
        }
        /* correct nextlinesize, remove the lf we added while recording */
        scriptdata->nextline_c--;
      }
    }
    return TRUE;
}