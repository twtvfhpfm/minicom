/*
 * cmdsave.c	Saved serial commands for Send Command (Ctrl+A D).
 *
 *		This file is part of the minicom communications package.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "port.h"
#include "minicom.h"
#include "intl.h"

#define SAVED_CMD_FILE     ".minicom_cmds"
#define MAX_SAVED_CMDS     64
#define SAVED_CMD_LEN      256
#define INPUT_MAX_LEN      1024
#define INPUT_LINE_LEN     38
#define LIST_VISIBLE       8
#define LIST_Y_START       1
#define WIN_X_HALF         30

#define MODE_INPUT         0
#define MODE_COMMAND       1

#define CMD_ACT_SEND       1
#define CMD_ACT_CANCEL     2

static char saved_cmds[MAX_SAVED_CMDS][SAVED_CMD_LEN];
static int saved_cmd_count;
static int cmds_loaded;

static const char *d_yesno[] = { N_("   Yes  "), N_("   No   "), NULL };

static void cmdfile_path(char *path, size_t pathlen)
{
  snprintf(path, pathlen, "%s/%s", homedir, SAVED_CMD_FILE);
}

static int load_saved_cmds(void)
{
  char path[512];
  FILE *fp;
  char line[SAVED_CMD_LEN + 4];

  saved_cmd_count = 0;
  cmdfile_path(path, sizeof(path));
  fp = fopen(path, "r");
  if (fp == NULL) {
    return 0;
  }

  while (saved_cmd_count < MAX_SAVED_CMDS &&
         fgets(line, sizeof(line), fp) != NULL) {
    size_t len;

    len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }
    if (line[0] == '\0' || len >= SAVED_CMD_LEN) {
      continue;
    }
    snprintf(saved_cmds[saved_cmd_count], SAVED_CMD_LEN, "%s", line);
    saved_cmd_count++;
  }
  fclose(fp);
  cmds_loaded = 1;
  return 0;
}

static int save_saved_cmds(void)
{
  char path[512];
  FILE *fp;
  int i;
  int omask;

  cmdfile_path(path, sizeof(path));
  omask = umask(077);
  fp = fopen(path, "w");
  umask(omask);
  if (fp == NULL) {
    werror(_("Cannot write saved commands file"));
    return -1;
  }
  for (i = 0; i < saved_cmd_count; i++) {
    fprintf(fp, "%s\n", saved_cmds[i]);
  }
  fclose(fp);
  return 0;
}

static int add_saved_cmd(const char *cmd)
{
  int i;

  if (cmd == NULL || cmd[0] == '\0') {
    return -1;
  }
  if (strchr(cmd, '\n') != NULL || strchr(cmd, '\r') != NULL) {
    return -1;
  }
  if (strlen(cmd) >= SAVED_CMD_LEN) {
    werror(_("Command too long to save"));
    return -1;
  }
  for (i = 0; i < saved_cmd_count; i++) {
    if (strcmp(saved_cmds[i], cmd) == 0) {
      return 0;
    }
  }
  if (saved_cmd_count >= MAX_SAVED_CMDS) {
    werror(_("Saved command list is full"));
    return -1;
  }
  snprintf(saved_cmds[saved_cmd_count], SAVED_CMD_LEN, "%s", cmd);
  saved_cmd_count++;
  if (save_saved_cmds() != 0) {
    saved_cmd_count--;
    saved_cmds[saved_cmd_count][0] = '\0';
    return -1;
  }
  return 1;
}

static int delete_saved_cmd(int idx)
{
  int i;

  if (idx < 0 || idx >= saved_cmd_count) {
    return -1;
  }
  for (i = idx; i < saved_cmd_count - 1; i++) {
    snprintf(saved_cmds[i], SAVED_CMD_LEN, "%s", saved_cmds[i + 1]);
  }
  saved_cmd_count--;
  return save_saved_cmds();
}

static void send_command_string(const char *s)
{
  size_t i;
  size_t len;

  if (s == NULL) {
    return;
  }
  len = strlen(s);
  for (i = 0; i < len; i++) {
    vt_send((unsigned char)s[i]);
  }
}

static int list_visible_count(void)
{
  int visible;

  visible = saved_cmd_count;
  if (visible > LIST_VISIBLE) {
    visible = LIST_VISIBLE;
  }
  if (visible < 1) {
    visible = 1;
  }
  return visible;
}

static int win_content_width(WIN *w)
{
  int width = w->xs - 2;

  if (width < 20) {
    width = 20;
  }
  return width;
}

static void win_layout(int list_rows, int *win_y1, int *win_y2,
                       int *input_y, int *hint_y)
{
  *win_y1 = 4;
  *input_y = LIST_Y_START + list_rows;
  *hint_y = *input_y + 1;
  /* ys = hint_y + 1, win_y2 = y1 + ys - 1 */
  *win_y2 = *win_y1 + *hint_y;
}

static void redraw_input_line(WIN *w, int x, int y, const char *buf, int linelen)
{
  int i;
  size_t len;

  len = strlen(buf);
  if ((int)len > linelen) {
    len = linelen;
  }
  mc_wlocate(w, x, y);
  for (i = 0; i < linelen; i++) {
    mc_wputc(w, (i < (int)len) ? (wchar_t)buf[i] : L' ');
  }
}

static void draw_cmd_list(WIN *w, int sel_idx, int y_start, int list_slots, int cmd_mode)
{
  int top;
  int i;
  int row;
  int width;
  int viewport;

  width = win_content_width(w);
  viewport = saved_cmd_count;
  if (viewport > list_slots) {
    viewport = list_slots;
  }

  if (saved_cmd_count <= 0) {
    for (i = 0; i < list_slots; i++) {
      mc_wlocate(w, 1, y_start + i);
      mc_wclreol(w);
      if (i == 0) {
        if (cmd_mode) {
          mc_wsetattr(w, XA_REVERSE | stdattr);
          mc_wprintf(w, "%-*.*s", width, width, _("(no saved commands)"));
          mc_wsetattr(w, stdattr);
        } else {
          mc_wprintf(w, "%-*.*s", width, width, _("(no saved commands)"));
        }
      }
    }
    return;
  }

  top = 0;
  if (sel_idx >= viewport) {
    top = sel_idx - viewport + 1;
  }
  if (top + viewport > saved_cmd_count) {
    top = saved_cmd_count - viewport;
  }
  if (top < 0) {
    top = 0;
  }

  for (i = 0; i < list_slots; i++) {
    row = y_start + i;
    mc_wlocate(w, 1, row);
    mc_wclreol(w);
    if (top + i >= saved_cmd_count) {
      continue;
    }
    if (top + i == sel_idx) {
      mc_wsetattr(w, XA_REVERSE | stdattr);
      mc_wprintf(w, ">%.*s", width - 1, saved_cmds[top + i]);
      mc_wsetattr(w, stdattr);
    } else {
      mc_wprintf(w, " %.*s", width - 1, saved_cmds[top + i]);
    }
  }
}

static void draw_hint(WIN *w, int hint_y, int cmd_mode)
{
  const char *msg;
  char buf[96];
  int width;

  width = win_content_width(w);
  msg = cmd_mode
      ? _("j/k:mv s:save d:del Enter:send Esc:back C-c:quit")
      : _("Esc:cmd Enter:send C-c:quit");
  mc_wlocate(w, 1, hint_y);
  mc_wclreol(w);
  snprintf(buf, sizeof(buf), "%-*.*s", width, width, msg);
  mc_wputs(w, buf);
}

static void select_cmd(int idx, char *input_buf, size_t input_len)
{
  if (idx < 0 || idx >= saved_cmd_count) {
    return;
  }
  snprintf(input_buf, input_len, "%s", saved_cmds[idx]);
}

static void move_selection(int *sel_idx, int delta)
{
  if (saved_cmd_count <= 0) {
    *sel_idx = -1;
    return;
  }
  if (*sel_idx < 0) {
    *sel_idx = (delta > 0) ? 0 : saved_cmd_count - 1;
    return;
  }
  *sel_idx += delta;
  if (*sel_idx < 0) {
    *sel_idx = saved_cmd_count - 1;
  } else if (*sel_idx >= saved_cmd_count) {
    *sel_idx = 0;
  }
}

static int cmd_input_loop(WIN *w, int list_y, int input_y, int hint_y,
                          int list_slots, char *input_buf, int *sel_idx)
{
  int x = 2;
  int y = input_y;
  int idx = 0;
  int offs = 0;
  int linelen = INPUT_LINE_LEN;
  int c;
  int st = CMD_ACT_CANCEL;
  int quit = 0;
  int once = 1;
  int overwrite = 1;
  int direct = dirflush;
  int cmd_mode = MODE_INPUT;
  int i;

  if (linelen > win_content_width(w) - 2) {
    linelen = win_content_width(w) - 2;
  }
  dirflush = 0;
  draw_hint(w, hint_y, cmd_mode);
  mc_wlocate(w, x + idx, y);
  mc_wflush();

  while (!quit) {
    if (once) {
      c = K_END;
      once = 0;
    } else {
      c = wxgetch();
      if (cmd_mode == MODE_INPUT &&
          (c > 255 || c == K_BS || c == K_DEL)) {
        overwrite = 0;
      }
    }

    if (c == 3) {
      st = CMD_ACT_CANCEL;
      quit = 1;
      continue;
    }

    if (cmd_mode == MODE_COMMAND) {
      switch (c) {
      case '\r':
      case '\n':
        st = CMD_ACT_SEND;
        quit = 1;
        break;
      case K_ESC:
        cmd_mode = MODE_INPUT;
        draw_cmd_list(w, *sel_idx, list_y, list_slots, cmd_mode);
        draw_hint(w, hint_y, cmd_mode);
        mc_wlocate(w, x + idx - offs, y);
        mc_wflush();
        break;
      case 'j':
      case K_DN:
        move_selection(sel_idx, 1);
        select_cmd(*sel_idx, input_buf, INPUT_MAX_LEN);
        idx = strlen(input_buf);
        offs = 0;
        draw_cmd_list(w, *sel_idx, list_y, list_slots, cmd_mode);
        redraw_input_line(w, x, y, input_buf, linelen);
        mc_wlocate(w, x + idx, y);
        mc_wflush();
        break;
      case 'k':
      case K_UP:
        move_selection(sel_idx, -1);
        select_cmd(*sel_idx, input_buf, INPUT_MAX_LEN);
        idx = strlen(input_buf);
        offs = 0;
        draw_cmd_list(w, *sel_idx, list_y, list_slots, cmd_mode);
        redraw_input_line(w, x, y, input_buf, linelen);
        mc_wlocate(w, x + idx, y);
        mc_wflush();
        break;
      case 's':
        if (input_buf[0] != '\0') {
          i = add_saved_cmd(input_buf);
          if (i == 1) {
            werror(_("Command saved"));
            if (*sel_idx < 0 && saved_cmd_count > 0) {
              *sel_idx = saved_cmd_count - 1;
            }
            draw_cmd_list(w, *sel_idx, list_y, list_slots, cmd_mode);
          }
        }
        break;
      case 'd':
        if (saved_cmd_count > 0 && *sel_idx >= 0 &&
            ask(_("Remove saved command?"), d_yesno) == 0) {
          delete_saved_cmd(*sel_idx);
          if (saved_cmd_count == 0) {
            *sel_idx = -1;
            input_buf[0] = '\0';
            idx = 0;
            offs = 0;
          } else {
            if (*sel_idx >= saved_cmd_count) {
              *sel_idx = saved_cmd_count - 1;
            }
            select_cmd(*sel_idx, input_buf, INPUT_MAX_LEN);
            idx = strlen(input_buf);
            offs = 0;
          }
          draw_cmd_list(w, *sel_idx, list_y, list_slots, cmd_mode);
          redraw_input_line(w, x, y, input_buf, linelen);
          mc_wlocate(w, x + idx - offs, y);
          mc_wflush();
        }
        break;
      default:
        break;
      }
      continue;
    }

    /* Input mode */
    switch (c) {
    case '\r':
    case '\n':
      st = CMD_ACT_SEND;
      quit = 1;
      break;
    case K_ESC:
      cmd_mode = MODE_COMMAND;
      if (input_buf[0] == '\0' && saved_cmd_count > 0) {
        *sel_idx = 0;
        select_cmd(*sel_idx, input_buf, INPUT_MAX_LEN);
        idx = strlen(input_buf);
        offs = 0;
        overwrite = 0;
        redraw_input_line(w, x, y, input_buf, linelen);
      } else if (input_buf[0] != '\0') {
        *sel_idx = -1;
      }
      draw_cmd_list(w, *sel_idx, list_y, list_slots, cmd_mode);
      draw_hint(w, hint_y, cmd_mode);
      mc_wlocate(w, x + idx - offs, y);
      mc_wflush();
      break;
    case K_HOME:
      idx = 0;
      offs = 0;
      redraw_input_line(w, x, y, input_buf, linelen);
      mc_wlocate(w, x, y);
      mc_wflush();
      break;
    case K_END:
      idx = strlen(input_buf);
      offs = 0;
      if (idx > linelen) {
        offs = idx - linelen;
      }
      redraw_input_line(w, x, y, input_buf + offs, linelen);
      mc_wlocate(w, x + idx - offs, y);
      mc_wflush();
      break;
    case K_LT:
    case K_BS:
      if (idx == 0) {
        break;
      }
      idx--;
      if (idx < offs) {
        offs -= 4;
        if (offs < 0) {
          offs = 0;
        }
        redraw_input_line(w, x, y, input_buf + offs, linelen);
      }
      if (c == K_LT) {
        mc_wlocate(w, x + idx - offs, y);
        mc_wflush();
        break;
      }
      /* fall through */
    case K_DEL:
      if (input_buf[idx] == '\0') {
        break;
      }
      memmove(input_buf + idx, input_buf + idx + 1,
              strlen(input_buf + idx));
      redraw_input_line(w, x, y, input_buf + offs, linelen);
      mc_wlocate(w, x + idx - offs, y);
      mc_wflush();
      break;
    case K_RT:
      if (input_buf[idx] == '\0') {
        break;
      }
      idx++;
      if (idx - offs > linelen) {
        offs += 4;
        redraw_input_line(w, x, y, input_buf + offs, linelen);
      }
      mc_wlocate(w, x + idx - offs, y);
      mc_wflush();
      break;
    default:
      if (c < 32 || c > 255) {
        break;
      }
      if (overwrite) {
        input_buf[0] = '\0';
        idx = 0;
        offs = 0;
        overwrite = 0;
        redraw_input_line(w, x, y, input_buf, linelen);
      }
      if (strlen(input_buf) + 1 >= INPUT_MAX_LEN) {
        break;
      }
      memmove(input_buf + idx + 1, input_buf + idx,
              strlen(input_buf + idx) + 1);
      input_buf[idx] = (char)c;
      if (idx - offs >= linelen) {
        offs += 4;
        redraw_input_line(w, x, y, input_buf + offs, linelen);
      } else {
        redraw_input_line(w, x, y, input_buf + offs, linelen);
      }
      idx++;
      mc_wlocate(w, x + idx - offs, y);
      mc_wflush();
      break;
    }
  }

  dirflush = direct;
  return st;
}

void send_command_dialog(void)
{
  WIN *w;
  char input_buf[INPUT_MAX_LEN];
  int sel_idx;
  int list_rows;
  int list_slots;
  int win_y1;
  int win_y2;
  int input_y;
  int hint_y;
  int action;
  int direct;
  int win_x1;
  int win_x2;

  if (!cmds_loaded) {
    load_saved_cmds();
  }

  sel_idx = -1;
  input_buf[0] = '\0';

  list_rows = list_visible_count();
  list_slots = list_rows;
  win_layout(list_rows, &win_y1, &win_y2, &input_y, &hint_y);

  win_x1 = (COLS / 2) - WIN_X_HALF;
  win_x2 = (COLS / 2) + WIN_X_HALF;
  if (win_x1 < 1) {
    win_x1 = 1;
  }
  if (win_x2 >= COLS) {
    win_x2 = COLS - 1;
  }

  direct = dirflush;
  dirflush = 0;
  w = mc_wopen(win_x1, win_y1, win_x2, win_y2,
               BDOUBLE, stdattr, mfcolor, mbcolor, 1, 0, 1);
  if (w == NULL) {
    dirflush = direct;
    return;
  }

  mc_wtitle(w, TMID, _("Send Command"));
  draw_cmd_list(w, sel_idx, LIST_Y_START, list_slots, MODE_INPUT);

  mc_wlocate(w, 0, input_y);
  mc_wputs(w, "> ");
  redraw_input_line(w, 2, input_y, input_buf,
                    win_content_width(w) - 2);
  draw_hint(w, hint_y, MODE_INPUT);

  action = cmd_input_loop(w, LIST_Y_START, input_y, hint_y, list_slots,
                          input_buf, &sel_idx);
  mc_wclose(w, 1);
  dirflush = direct;

  if (action == CMD_ACT_SEND && input_buf[0] != '\0') {
    send_command_string(input_buf);
  }
}
