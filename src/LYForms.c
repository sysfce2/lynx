#include <HTUtils.h>
#include <HTCJK.h>
#include <HTTP.h>
#include <HTAlert.h>
#include <LYCurses.h>
#include <GridText.h>
#include <LYCharSets.h>
#include <UCAux.h>
#include <LYUtils.h>
#include <LYStructs.h>  /* includes HTForms.h */
#include <LYStrings.h>
#include <LYGlobalDefs.h>
#include <LYKeymap.h>
#include <LYClean.h>

#include <LYLeaks.h>

#ifdef USE_COLOR_STYLE
#include <AttrList.h>
#include <LYHash.h>
#endif

#if defined(VMS) && !defined(USE_SLANG)
#define CTRL_W_HACK DO_NOTHING
#else
#define CTRL_W_HACK 23  /* CTRL-W refresh without clearok */
#endif /* VMS && !USE_SLANG */

PRIVATE int form_getstr PARAMS((
	struct link *	form_link,
	BOOLEAN		use_last_tfpos,
	BOOLEAN		redraw_only));
PRIVATE int popup_options PARAMS((
	int		cur_selection,
	OptionType *	list,
	int		ly,
	int		lx,
	int		width,
	int		i_length,
	int		disabled));


PUBLIC int change_form_link_ex ARGS8(
	struct link *,	form_link,
	document *,	newdoc,
	BOOLEAN *,	refresh_screen,
	char *,		link_name,
	char *,		link_value,
	BOOLEAN,	use_last_tfpos,
	BOOLEAN,	immediate_submit,
	BOOLEAN,	redraw_only)
{
    FormInfo *form = form_link->form;
    int newdoc_changed = 0;
    int c = DO_NOTHING;
    int OrigNumValue;

	/*
	 *  If there is no form to perform action on, don't do anything.
	 */
	if (form == NULL) {
	    return(c);
	}

    /*
     *  Move to the link position.
     */
    move(form_link->ly, form_link->lx);

    switch(form->type) {
	case F_CHECKBOX_TYPE:
	    if (form->disabled == YES)
		break;
	    if (form->num_value) {
		form_link->hightext = unchecked_box;
		form->num_value = 0;
	    } else {
		form_link->hightext = checked_box;
		form->num_value = 1;
	    }
	    break;

	case F_OPTION_LIST_TYPE:
	    if (!form->select_list) {
		HTAlert(BAD_HTML_NO_POPUP);
		c = DO_NOTHING;
		break;
	    }

	    if (form->disabled == YES) {
		int dummy;
		dummy = popup_options(form->num_value, form->select_list,
				form_link->ly, form_link->lx, form->size,
				form->size_l, form->disabled);
#if CTRL_W_HACK != DO_NOTHING
		if (!enable_scrollback)
		    c = CTRL_W_HACK;  /* CTRL-W refresh without clearok */
		else
#endif
		    c = 12;  /* CTRL-L for repaint */
		break;
	    }
	    OrigNumValue = form->num_value;
	    form->num_value = popup_options(form->num_value, form->select_list,
				form_link->ly, form_link->lx, form->size,
				form->size_l, form->disabled);

	    {
		OptionType * opt_ptr = form->select_list;
		int i;
		for (i = 0; i < form->num_value; i++, opt_ptr = opt_ptr->next)
		    ; /* null body */
		/*
		 *  Set the name.
		 */
		form->value = opt_ptr->name;
		/*
		 *  Set the value.
		 */
		form->cp_submit_value = opt_ptr->cp_submit_value;
		 /*
		  *  Set charset in which we have the submit value. - kw
		  */
		form->value_cs = opt_ptr->value_cs;
	    }
#if CTRL_W_HACK != DO_NOTHING
	    if (!enable_scrollback)
		c = CTRL_W_HACK;	 /* CTRL-W refresh without clearok */
	    else
#endif
		c = 12;	 /* CTRL-L for repaint */
	    break;

	case F_RADIO_TYPE:
	    if (form->disabled == YES)
		break;
		/*
		 *  Radio buttons must have one and
		 *  only one down at a time!
		 */
	    if (form->num_value) {
		HTUserMsg(NEED_CHECKED_RADIO_BUTTON);
	    } else {
		int i;
		/*
		 *  Run though list of the links on the screen and
		 *  unselect any that are selected. :)
		 */
		lynx_start_radio_color ();
		for (i = 0; i < nlinks; i++) {
		    if (links[i].type == WWW_FORM_LINK_TYPE &&
			links[i].form->type == F_RADIO_TYPE &&
			links[i].form->number == form->number &&
			/*
			 *  If it has the same name and its on...
			 */
			!strcmp(links[i].form->name, form->name) &&
			links[i].form->num_value) {
			move(links[i].ly, links[i].lx);
			addstr(unchecked_radio);
			links[i].hightext = unchecked_radio;
		    }
		}
		lynx_stop_radio_color ();
		/*
		 *  Will unselect other button and select this one.
		 */
		HText_activateRadioButton(form);
		/*
		 *  Now highlight this one.
		 */
		form_link->hightext = checked_radio;
	    }
	    break;

	case F_FILE_TYPE:
	case F_TEXT_TYPE:
	case F_TEXTAREA_TYPE:
	case F_PASSWORD_TYPE:
	    c = form_getstr(form_link, use_last_tfpos, redraw_only);
	    if (form->type == F_PASSWORD_TYPE)
		form_link->hightext = STARS(strlen(form->value));
	    else
		form_link->hightext = form->value;
	    break;

	case F_RESET_TYPE:
	    if (form->disabled == YES)
		break;
	    HText_ResetForm(form);
	    *refresh_screen = TRUE;
	    break;

	case F_TEXT_SUBMIT_TYPE:
	    if (redraw_only) {
		c = form_getstr(form_link, use_last_tfpos, TRUE);
		break;
	    }
	    if (!immediate_submit)
		c = form_getstr(form_link, use_last_tfpos, FALSE);
	    if (form->disabled == YES &&
		(c == '\r' || c == '\n' || immediate_submit)) {
		if (peek_mouse_link() >= 0)
		    c = lookup_keymap(LYK_ACTIVATE);
		else
		c = '\t';
		break;
	    }
	    /*
	     *  If immediate_submit is set, we didn't enter the line editor
	     *  above, and will now try to call HText_SubmitForm() directly.
	     *  If immediate_submit is not set, c is the lynxkeycode returned
	     *  from line editing.   Then if c indicates that a key was pressed
	     *  that means we should submit, but with some extra considerations
	     *  (i.e. NOCACHE, DOWNLOAD, different from simple Enter), or if
	     *  we should act on some *other* link selected with the mouse,
	     *  we'll just return c and leave it to mainloop() to do the
	     *  right thing; if everything checks out, it should call this
	     *  function again, with immediate_submit set.
	     *  If c indicates that line editing ended with Enter, we still
	     *  defer to mainloop() for further checking if the submit
	     *  action URL could require more checks than we do here.
	     *  Only in the remaining cases do we proceed to call
	     *  HText_SubmitForm() directly before returning. - kw
	     */
	    if (immediate_submit ||
		((c == '\r' || c == '\n' || c == LAC_TO_LKC0(LYK_SUBMIT)) &&
		 peek_mouse_link() == -1)) {
		form_link->hightext = form->value;
#ifdef TEXT_SUBMIT_CONFIRM_WANTED
		if (!immediate_submit && (c == '\r' || c == '\n') &&
		    !HTConfirmDefault(NO_SUBMIT_BUTTON_QUERY), YES) {
		    /* User was prompted and declined; if canceled with ^G
		     * let mainloop stay on this field, otherwise move on to
		     * the next field or link. - kw
		     */
		    if (HTLastConfirmCancelled())
			c = DO_NOTHING;
		    else
			c = LAC_TO_LKC(LYK_NEXT_LINK);
		    break;
		}
#endif
		if (!form->submit_action || *form->submit_action == '\0') {
		    HTUserMsg(NO_FORM_ACTION);
		    c = DO_NOTHING;
		    break;
		} else if (form->submit_method == URL_MAIL_METHOD && no_mail) {
		    HTAlert(FORM_MAILTO_DISALLOWED);
		    c = DO_NOTHING;
		    break;
		} else if (!immediate_submit &&
			   ((no_file_url &&
			     !strncasecomp(form->submit_action, "file:", 5)) ||
			    !strncasecomp(form->submit_action, "lynx", 4))) {
		    c = LAC_TO_LKC0(LYK_SUBMIT);
		    break;
		} else {
		    if (form->no_cache &&
			form->submit_method != URL_MAIL_METHOD) {
			LYforce_no_cache = TRUE;
			reloading = TRUE;
		    }
		    newdoc_changed =
			HText_SubmitForm(form, newdoc, link_name, form->value);
		}
		if (form->submit_method == URL_MAIL_METHOD) {
		    *refresh_screen = TRUE;
		} else {
		    /*
		     *  Returns new document URL.
		     */
		    newdoc->link = 0;
		    newdoc->internal_link = FALSE;
		}
		c = DO_NOTHING;
		break;
	    } else {
		form_link->hightext = form->value;
	    }
	    break;

	case F_SUBMIT_TYPE:
	case F_IMAGE_SUBMIT_TYPE:
	    if (form->disabled == YES)
		break;
	    if (form->no_cache &&
		form->submit_method != URL_MAIL_METHOD) {
		LYforce_no_cache = TRUE;
		reloading = TRUE;
	    }
	    newdoc_changed =
		HText_SubmitForm(form, newdoc, link_name, link_value);
	    if (form->submit_method == URL_MAIL_METHOD)
		*refresh_screen = TRUE;
	    else {
		/* returns new document URL */
		newdoc->link = 0;
		newdoc->internal_link = FALSE;
	    }
	    break;

    }

    if (newdoc_changed) {
	c = LKC_DONE;
    } else {
	/*
	 *  These flags may have been set in mainloop, anticipating that
	 *  a request will be submitted.  But if we haven't filled in
	 *  newdoc, that won't actually be the case, so unset them. - kw
	 */
	LYforce_no_cache = FALSE;
	reloading = FALSE;
    }
    return(c);
}

PUBLIC int change_form_link ARGS7(
	struct link *,	form_link,
	document *,	newdoc,
	BOOLEAN *,	refresh_screen,
	char *,		link_name,
	char *,		link_value,
	BOOLEAN,	use_last_tfpos,
	BOOLEAN,	immediate_submit)
{
    /*pass all our args and FALSE as last arg*/
    return change_form_link_ex(form_link,newdoc,refresh_screen,link_name,
	link_value,use_last_tfpos,immediate_submit, FALSE /*redraw_only*/ );
}

PRIVATE int LastTFPos = -1;	/* remember last text field position */

PRIVATE void LYSetLastTFPos ARGS1(
    int,	pos)
{
    LastTFPos = pos;
}

PRIVATE int form_getstr ARGS3(
	struct link *,	form_link,
	BOOLEAN,	use_last_tfpos,
	BOOLEAN,	redraw_only)
{
    FormInfo *form = form_link->form;
    char *value = form->value;
    int ch;
    int far_col;
    int max_length;
    int startcol, startline;
    BOOL HaveMaxlength = FALSE;
    int action, repeat;
    int last_xlkc = -1;
#ifdef SUPPORT_MULTIBYTE_EDIT
    BOOL refresh_mb = TRUE;
#endif

    EditFieldData MyEdit;
    BOOLEAN Edited = FALSE;		/* Value might be updated? */

    /*
     *  Get the initial position of the cursor.
     */
    LYGetYX(startline, startcol);
    if ((startcol + form->size) > (LYcols - 1))
	far_col = (LYcols - 1);
    else
	far_col = (startcol + form->size);

    /*
     *  Make sure the form field value does not exceed our buffer. - FM
     */
    max_length = ((form->maxlength > 0 &&
		   form->maxlength < sizeof(MyEdit.buffer)) ?
					    form->maxlength :
					    (sizeof(MyEdit.buffer) - 1));
    if (strlen(form->value) > (size_t)max_length) {
	/*
	 *  We can't fit the entire value into the editing buffer,
	 *  so enter as much of the tail as fits. - FM
	 */
	value += (strlen(form->value) - max_length);
	if (!form->disabled &&
	    !(form->submit_method == URL_MAIL_METHOD && no_mail)) {
	    /*
	     *  If we can edit it, report that we are using the tail. - FM
	     */
	    HTUserMsg(FORM_VALUE_TOO_LONG);
	    show_formlink_statusline(form, redraw_only? FOR_PANEL : FOR_INPUT);
	    move(startline, startcol);
	}
    }

    /*
     *  Print panned line
     */
    LYSetupEdit(&MyEdit, value, max_length, (far_col - startcol));
    MyEdit.pad = '_';
    MyEdit.hidden = (BOOL) (form->type == F_PASSWORD_TYPE);
    if (use_last_tfpos && LastTFPos >= 0 && LastTFPos < MyEdit.strlen) {
#if defined(TEXTFIELDS_MAY_NEED_ACTIVATION) && defined(INACTIVE_INPUT_STYLE_VH)
	if (redraw_only) {
	    if (!(MyEdit.strlen >= MyEdit.dspwdth &&
		  LastTFPos >= MyEdit.dspwdth - MyEdit.margin)) {
		MyEdit.pos = LastTFPos;
		if (MyEdit.strlen >= MyEdit.dspwdth)
		    textinput_redrawn = FALSE;
	    }
	} else
#endif /* TEXTFIELDS_MAY_NEED_ACTIVATION && INACTIVE_INPUT_STYLE_VH */
	    MyEdit.pos = LastTFPos;
#ifdef ENHANCED_LINEEDIT
	if (MyEdit.pos == 0)
	    MyEdit.mark = MyEdit.strlen;
#endif
    }
    /* Try to prepare for setting position based on the last mouse event */
#if defined(TEXTFIELDS_MAY_NEED_ACTIVATION) && defined(INACTIVE_INPUT_STYLE_VH)
    if (!redraw_only) {
	if (peek_mouse_levent()) {
	    if (!use_last_tfpos && !textinput_redrawn) {
		MyEdit.pos = 0;
	    }
	}
	textinput_redrawn = FALSE;
    }
#else
    if (peek_mouse_levent()) {
	if (!use_last_tfpos)
	    MyEdit.pos = 0;
    }
#endif /* TEXTFIELDS_MAY_NEED_ACTIVATION && INACTIVE_INPUT_STYLE_VH */
    LYRefreshEdit(&MyEdit);
    if (redraw_only)
	return 0;		/*return value won't be analysed*/

    /*
     *  And go for it!
     */
    for (;;) {
again:
	repeat = -1;
	get_mouse_link();	/* Reset mouse_link. */

	ch = LYgetch_input();
#ifdef SUPPORT_MULTIBYTE_EDIT
	if (!refresh_mb
	 && (EditBinding(ch) != LYE_CHAR)
#ifndef WIN_EX
	 && (EditBinding(ch) != LYE_AIX)
#endif
	    )
	    goto again;
#endif /* SUPPORT_MULTIBYTE_EDIT */
#ifdef VMS
	if (HadVMSInterrupt) {
	    HadVMSInterrupt = FALSE;
	    ch = 7;
	}
#endif /* VMS */

#ifdef USE_MOUSE
#  if defined(NCURSES) || defined(PDCURSES)
	if (ch != -1 && (ch & LKC_ISLAC) && !(ch & LKC_ISLECLAC)) /* already lynxactioncode? */
	    break;	/* @@@ maybe move these 2 lines outside ifdef -kw */
	if (ch == MOUSE_KEY) {		/* Need to process ourselves */
#if defined(PDCURSES)
	    int curx, cury;

	    request_mouse_pos();
	    LYGetYX(cury, curx);
	    if (MOUSE_Y_POS == cury) {
		repeat = MOUSE_X_POS - curx;
		if (repeat < 0) {
		    ch = LTARROW;
		    repeat = - repeat;
		} else
		    ch = RTARROW;
	    }
#else
	    MEVENT	event;
	    int curx, cury;

	    getmouse(&event);
	    LYGetYX(cury, curx);
	    if (event.y == cury) {
		repeat = event.x - curx;
		if (repeat < 0) {
		    ch = LTARROW;
		    repeat = - repeat;
		} else
		    ch = RTARROW;
	    }
#endif /* PDCURSES */
	    else {
		/*  Mouse event passed to us as MOUSE_KEY, and apparently
		 *  not on this field's line?  Something is not as it
		 *  should be...
		 *  A call to statusline() may have happened, possibly from
		 *  within a mouse menu.  Let's at least make sure here
		 *  that the cursor position gets restored.  - kw
		 */
		MyEdit.dirty = TRUE;
	    }
	    last_xlkc = -1;
	} else
#  endif     /* NCURSES || PDCURSES */
#endif /* USE_MOUSE */

	{
	    if (!(ch & LKC_ISLECLAC))
		ch |= MyEdit.current_modifiers;
	    MyEdit.current_modifiers = 0;
	    if (last_xlkc != -1) {
		if (ch == last_xlkc)
		    ch |= LKC_MOD3;
		last_xlkc = -1;	/* consumed */
	    }
	}
	if (peek_mouse_link() != -1)
	    break;

	action = EditBinding(ch);
	if ((action & LYE_DF) && !(action & LYE_FORM_LAC)) {
	    last_xlkc = ch;
	    action &= ~LYE_DF;
	} else {
	    last_xlkc = -1;
	}

	if (action == LYE_SETM1) {
	    /*
	     *  Set flag for modifier 1.
	     */
	    MyEdit.current_modifiers |= LKC_MOD1;
	    continue;
	}
	if (action == LYE_SETM2) {
	    /*
	     *  Set flag for modifier 2.
	     */
	    MyEdit.current_modifiers |= LKC_MOD2;
	    continue;
	}
	/*
	 *  Filter out global navigation keys that should not be passed
	 *  to line editor, and LYK_REFRESH.
	 */
	if (action == LYE_ENTER)
	    break;
	if (action == LYE_FORM_PASS)
	    break;
	if (action & LYE_FORM_LAC) {
	    ch = (action & LAC_MASK) | LKC_ISLAC;
	    break;
	}
	if (action == LYE_LKCMD) {
	    _statusline(ENTER_LYNX_COMMAND);
	    ch = LYgetch();
#ifdef VMS
	    if (HadVMSInterrupt) {
		HadVMSInterrupt = FALSE;
		ch = 7;
	    }
#endif /* VMS */
	    break;
	}

#if defined(WIN_EX)	/* 1998/10/01 (Thu) 19:19:22 */

#define FORM_PASTE_MAX	8192

	if (action == LYE_PASTE) {
	    unsigned char buff[FORM_PASTE_MAX];
	    int i, len;

	    len = get_clip(buff, FORM_PASTE_MAX);

	    if (len > 0) {
		i = 0;
		while ((ch = buff[i]) != '\0') {

		    if (ch == '\r') {
			i++;
			continue;
		    }
		    if (ch == '\n') {
			i++;
			len = strlen(buff + i);
			if (len > 0) {
			    put_clip(buff + i);
			}
			break;
		    }

		    LYLineEdit(&MyEdit, ch, TRUE);

		    if (MyEdit.strlen >= max_length) {
			HaveMaxlength = TRUE;
		    } else if (HaveMaxlength &&
			       MyEdit.strlen < max_length) {
			HaveMaxlength = FALSE;
			_statusline(ENTER_TEXT_ARROWS_OR_TAB);
		    }
		    i++;
		}
		if (strcmp(value, MyEdit.buffer) != 0) {
		    Edited = TRUE;
		}
		LYRefreshEdit(&MyEdit);

	    } else {
		HTInfoMsg("Clipboard empty or Not text data.");
		return(DO_NOTHING);
	    }
	    break;
	}
#else
	if (action == LYE_AIX &&
	    (HTCJK == NOCJK && LYlowest_eightbit[current_char_set] > 0x97))
	    break;
#endif
	if (action == LYE_TAB) {
	    ch = (int)('\t');
	    break;
	}
	if (action == LYE_ABORT) {
	    return(DO_NOTHING);
	}
	if (LKC_TO_LAC(keymap,ch) == LYK_REFRESH)
	    break;
#ifdef SH_EX
/* ASATAKU emacskey hack 1997/08/26 (Tue) 09:19:23 */
	if (emacs_keys &&
	    (EditBinding(ch) == LYE_FORWW || EditBinding(ch) == LYE_BACKW))
	    goto breakfor;
/* ASATAKU emacskey hack */
#endif
	switch (ch) {
#ifdef NOTDEFINED	/* The first four are mapped to LYE_FORM_PASS now */
	    case DNARROW:
	    case UPARROW:
	    case PGUP:
	    case PGDOWN:
	    case HOME:
	    case END_KEY:
	    case FIND_KEY:
	    case SELECT_KEY:
		goto breakfor;
#endif /* NOTDEFINED */

	    /*
	     *  Left arrrow in column 0 deserves special treatment here,
	     *  else you can get trapped in a form without submit button!
	     */
	    case LTARROW:	/* 1999/04/14 (Wed) 15:01:33 */
		if (MyEdit.pos == 0 && repeat == -1) {
		    int c = YES;    /* Go back immediately if no changes */
		    if (textfield_prompt_at_left_edge) {
			c = HTConfirmDefault(PREV_DOC_QUERY, NO);
		    } else if (strcmp(MyEdit.buffer, value)) {
			c = HTConfirmDefault(PREV_DOC_QUERY, NO);
		    }
		    if (c == YES) {
			return(ch);
		    } else {
			if (form->disabled == YES)
			    _statusline(ARROWS_OR_TAB_TO_MOVE);
			else
			    _statusline(ENTER_TEXT_ARROWS_OR_TAB);
		    }
		}
		/* fall through */

	    default:
		if (form->disabled == YES) {
		    /*
		     *  Allow actions that don't modify the contents even
		     *  in disabled form fields, so the user can scroll
		     *  through the line for reading if necessary. - kw
		     */
		    switch(action) {
		    case LYE_BOL:
		    case LYE_EOL:
		    case LYE_FORW:
		    case LYE_BACK:
		    case LYE_FORWW:
		    case LYE_BACKW:
#ifdef EXP_KEYBOARD_LAYOUT
		    case LYE_SWMAP:
#endif
#ifdef ENHANCED_LINEEDIT
		    case LYE_SETMARK:
		    case LYE_XPMARK:
#endif
			break;
		    default:
			goto again;
		    }
		}
		/*
		 *  Make sure the statusline uses editmode help.
		 */
		if (repeat < 0)
		    repeat = 1;
		while (repeat--) {
#ifndef SUPPORT_MULTIBYTE_EDIT
		    LYLineEdit(&MyEdit, ch, TRUE);
#else /* SUPPORT_MULTIBYTE_EDIT */
		    if (LYLineEdit(&MyEdit, ch, TRUE) == 0) {
			if (HTCJK != NOCJK && (0x80 <= ch)
			&& (ch <= 0xfe) && refresh_mb)
			    refresh_mb = FALSE;
			else
			    refresh_mb = TRUE;
		    } else {
			if (!refresh_mb) {
			    LYEdit1(&MyEdit, 0, LYE_DELP, TRUE);
			}
		    }
#endif /* SUPPORT_MULTIBYTE_EDIT */
		}
		if (MyEdit.strlen >= max_length) {
		    HaveMaxlength = TRUE;
		} else if (HaveMaxlength &&
			   MyEdit.strlen < max_length) {
		    HaveMaxlength = FALSE;
		    _statusline(ENTER_TEXT_ARROWS_OR_TAB);
		}
		if (strcmp(value, MyEdit.buffer)) {
		    Edited = TRUE;
		}
#ifdef SUPPORT_MULTIBYTE_EDIT
		if (refresh_mb)
#endif
		LYRefreshEdit(&MyEdit);
		LYSetLastTFPos(MyEdit.pos);
	}
    }
#if defined(NOTDEFINED) || defined(SH_EX)
breakfor:
#endif /* NOTDEFINED */
    if (Edited) {
	char  *p;

	/*
	 *  Load the new value.
	 */
	if (value == form->value) {
	    /*
	     *  The previous value did fit in the line buffer,
	     *  so replace it with the new value. - FM
	     */
	    StrAllocCopy(form->value, MyEdit.buffer);
	} else {
	    /*
	     *  Combine the modified tail with the unmodified head. - FM
	     */
	    form->value[(strlen(form->value) - strlen(value))] = '\0';
	    StrAllocCat(form->value, MyEdit.buffer);
	    HTUserMsg(FORM_TAIL_COMBINED_WITH_HEAD);
	}

	/*
	 *  Remove trailing spaces
	 *
	 *  Do we really need to do that here?  Trailing spaces will only
	 *  be there if user keyed them in.  Rather rude to throw away
	 *  their hard earned spaces.  Better deal with trailing spaces
	 *  when submitting the form????
	 */
	p = &(form->value[strlen(form->value)]);
	while ((p != form->value) && (p[-1] == ' '))
	    p--;
	*p = '\0';

	/*
	 *  If the field has been changed, assume that it is now in
	 *  current display character set, even if for some reason
	 *  it wasn't!  Hopefully a user will only submit the form
	 *  if the non-ASCII characters are displayed correctly, which
	 *  means (assuming that the display character set has been set
	 *  truthfully) the user confirms by changing the field that
	 *  the character encoding is right. - kw
	 */
	if (form->value && *form->value)
	    form->value_cs = current_char_set;
    }
    return(ch);
}

/*
**  This function prompts for an option or page number.
**  If a 'g' or 'p' suffix is included, that will be
**  loaded into c.  Otherwise, c is zeroed. - FM & LE
*/
PRIVATE int get_popup_option_number ARGS2(
	int *,		c,
	int *,		rel)
{
    char temp[120];
    char *p = temp;
    int num;

    /*
     *  Load the c argument into the prompt buffer.
     */
    temp[0] = (char) *c;
    temp[1] = '\0';
    _statusline(SELECT_OPTION_NUMBER);

    /*
     *  Get the number, possibly with a suffix, from the user.
     */
    if (LYgetstr(temp, VISIBLE, sizeof(temp), NORECALL) < 0 || *temp == 0) {
	HTInfoMsg(CANCELLED);
	*c = '\0';
	*rel = '\0';
	return(0);
    }

    *rel = '\0';
    num = atoi(p);
    while ( isdigit(*p) )
	++p;
    switch ( *p ) {
    case '+': case '-':
	/* 123+ or 123- */
	*rel = *p++; *c = *p;
	break;
    default:
	*c = *p++;
	*rel = *p;
	break;
    case 0:
	break;
    }

    /*
     *  If we had a 'g' or 'p' suffix, load it into c.
     *  Otherwise, zero c.  Then return the number.
     */
    if ( *p == 'g' || *p == 'G' ) {
	*c = 'g';
    } else if (*p == 'p' || *p == 'P' ) {
	*c = 'p';
    } else {
	*c = '\0';
    }
    if ( *rel != '+' && *rel != '-' )
	*rel = 0;
    return num;
}

PRIVATE void draw_option ARGS5(
	WINDOW *,	win,
	int,		entry,
	int,		width,
	BOOL,		reversed,
	OptionType *,	opt_ptr)
{
#ifdef USE_SLANG
    if (reversed)
	SLsmg_set_color(2);
    SLsmg_gotorc(win->top_y + entry, win->left_x + 2);
    SLsmg_write_nstring(opt_ptr->name, win->width);
    if (reversed)
	SLsmg_set_color(0);
#else
    wmove(win, entry, 2);
    if (reversed)
	wstart_reverse(win);
    LYpaddstr(win, width, opt_ptr->name);
    if (reversed)
	wstop_reverse(win);
#endif /* USE_SLANG */
}

PRIVATE int popup_options ARGS7(
	int,		cur_selection,
	OptionType *,	list,
	int,		ly,
	int,		lx,
	int,		width,
	int,		i_length,
	int,		disabled)
{
    /*
     *  Revamped to handle within-tag VALUE's, if present,
     *  and to position the popup window appropriately,
     *  taking the user_mode setting into account. -- FM
     */
    int c = 0, cmd = 0, i = 0, j = 0, rel = 0;
    int orig_selection = cur_selection;
    WINDOW * form_window;
    int num_options = 0, top, bottom, length = -1;
    OptionType * opt_ptr = list;
    int window_offset = 0;
    int lines_to_show;
    int npages;
    static char prev_target[512];		/* Search string buffer */
    static char prev_target_buffer[512];	/* Next search buffer */
    static BOOL first = TRUE;
    char *cp;
    int ch = 0, recall;
    int QueryTotal;
    int QueryNum;
    BOOLEAN FirstRecall = TRUE;
    OptionType * tmp_ptr;
    BOOLEAN ReDraw = FALSE;
    int number;

    /*
     * Initialize the search string buffer. - FM
     */
    if (first) {
	*prev_target_buffer = '\0';
	first = FALSE;
    }
    *prev_target = '\0';
    QueryTotal = (search_queries ? HTList_count(search_queries) : 0);
    recall = ((QueryTotal >= 1) ? RECALL : NORECALL);
    QueryNum = QueryTotal;

    /*
     *  Set lines_to_show based on the user_mode global.
     */
    if (user_mode == NOVICE_MODE)
	lines_to_show = LYlines-4;
    else
	lines_to_show = LYlines-2;

    /*
     *  Counting the number of options to be displayed.
     *   num_options ranges 0...n
     */
    for (; opt_ptr->next; num_options++, opt_ptr = opt_ptr->next)
	 ; /* null body */

    /*
     *  Let's assume for the sake of sanity that ly is the number
     *   corresponding to the line the selection box is on.
     *  Let's also assume that cur_selection is the number of the
     *   item that should be initially selected, as 0 beign the
     *   first item.
     *  So what we have, is the top equal to the current screen line
     *   subtracting the cur_selection + 1 (the one must be for the
     *   top line we will draw in a box).  If the top goes under 0,
     *   consider it 0.
     */
    top = ly - (cur_selection + 1);
    if (top < 0)
	top = 0;

    /*
     *  Check and see if we need to put the i_length parameter up to
     *  the number of real options.
     */
    if (!i_length) {
	i_length = num_options;
    } else {
	/*
	 *  Otherwise, it is really one number too high.
	 */
	i_length--;
    }

    /*
     *  The bottom is the value of the top plus the number of options
     *  to view plus 3 (one for the top line, one for the bottom line,
     *  and one to offset the 0 counted in the num_options).
     */
    bottom = top + i_length + 3;

    /*
     *  Hmm...  If the bottom goes beyond the number of lines available,
     */
    if (bottom > lines_to_show) {
	/*
	 *  Position the window at the top if we have more
	 *  options than will fit in the window.
	 */
	if (i_length+3 > lines_to_show) {
	    top = 0;
	    bottom = top + i_length+3;
	    if (bottom > lines_to_show)
		bottom = lines_to_show + 1;
	} else {
	    /*
	     *  Try to position the window so that the selected option will
	     *    appear where the selection box currently is positioned.
	     *  It could end up too high, at this point, but we'll move it
	     *    down latter, if that has happened.
	     */
	    top = (lines_to_show + 1) - (i_length + 3);
	    bottom = (lines_to_show + 1);
	}
    }

    /*
     *  This is really fun, when the length is 4, it means 0-4, or 5.
     */
    length = (bottom - top) - 2;

    /*
     *  Move the window down if it's too high.
     */
    if (bottom < ly + 2) {
	bottom = ly + 2;
	if (bottom > lines_to_show + 1)
	    bottom = lines_to_show + 1;
	top = bottom - length - 2;
    }

    /*
     *  Set up the overall window, including the boxing characters ('*'),
     *  if it all fits.  Otherwise, set up the widest window possible. - FM
     */
    if (width + 4 > LYcols) {
	lx = 1;
	width = LYcols - 5; /* avoids a crash? - kw */
    }
    if ((form_window = LYstartPopup(top, lx, bottom - top, width)) == 0)
	return(orig_selection);

    /*
     *  Set up the window_offset for options.
     *   cur_selection ranges from 0...n
     *   length ranges from 0...m
     */
    if (cur_selection >= length) {
	window_offset = cur_selection - length + 1;
    }

    /*
     *  Compute the number of popup window pages. - FM
     */
    npages = ((num_options + 1) > length) ?
		(((num_options + 1) + (length - 1))/(length))
					  : 1;
/*
 * OH!  I LOVE GOTOs! hack hack hack
 *	  07-11-94 GAB
 *      MORE hack hack hack
 *	  09-05-94 FM
 */
redraw:
    opt_ptr = list;

    /*
     *  Display the boxed options.
     */
    for (i = 0; i <= num_options; i++, opt_ptr = opt_ptr->next) {
	if (i >= window_offset && i - window_offset < length) {
	    draw_option(form_window, ((i + 1) - window_offset), width, FALSE, opt_ptr);
	}
    }
    LYbox(form_window, TRUE);
    opt_ptr = NULL;

    /*
     *  Loop on user input.
     */
    while (cmd != LYK_ACTIVATE) {
	int row = ((i + 1) - window_offset);

	/*
	 *  Unreverse cur selection.
	 */
	if (opt_ptr != NULL) {
	    draw_option(form_window, row, width, FALSE, opt_ptr);
	}

	opt_ptr = list;

	for (i = 0; i < cur_selection; i++, opt_ptr = opt_ptr->next)
	    ; /* null body */
	row = ((i + 1) - window_offset);

	draw_option(form_window, row, width, TRUE, opt_ptr);
	LYstowCursor(form_window, row, 1);

	c = LYgetch_choice();
	if (c == 7) {		/* Control-C or Control-G */
	    cmd = LYK_QUIT;
#ifndef USE_SLANG
	} else if (c == MOUSE_KEY) {
	    if ((cmd = fancy_mouse(form_window, row, &cur_selection)) < 0)
		goto redraw;
	    if  (cmd == LYK_ACTIVATE)
		break;
#endif
	} else {
	    cmd = LKC_TO_LAC(keymap,c);
	}
#ifdef VMS
	if (HadVMSInterrupt) {
	    HadVMSInterrupt = FALSE;
	    cmd = LYK_QUIT;
	}
#endif /* VMS */

	switch(cmd) {
	    case LYK_F_LINK_NUM:
		c = '\0';
		/* FALLTHRU */
	    case LYK_1: /* FALLTHRU */
	    case LYK_2: /* FALLTHRU */
	    case LYK_3: /* FALLTHRU */
	    case LYK_4: /* FALLTHRU */
	    case LYK_5: /* FALLTHRU */
	    case LYK_6: /* FALLTHRU */
	    case LYK_7: /* FALLTHRU */
	    case LYK_8: /* FALLTHRU */
	    case LYK_9:
		/*
		 *  Get a number from the user, possibly with
		 *  a 'g' or 'p' suffix (which will be loaded
		 *  into c). - FM & LE
		 */
		number = get_popup_option_number((int *)&c,(int *)&rel);

		/* handle + or - suffix */
		CTRACE((tfp,"got popup option number %d, ",number));
		CTRACE((tfp,"rel='%c', c='%c', cur_selection=%d\n",
				rel,c,cur_selection));
		if ( c == 'p' ) {
		    int curpage = ((cur_selection + 1) > length) ?
			(((cur_selection + 1) + (length - 1))/(length))
					  : 1;
		    CTRACE((tfp,"  curpage=%d\n",curpage));
		    if ( rel == '+' )
			number = curpage + number;
		    else if ( rel == '-' )
			number = curpage - number;
		} else if ( rel == '+' ) {
		    number = cur_selection + number + 1;
		} else if ( rel == '-' ) {
		    number = cur_selection - number + 1;
		}
		if ( rel ) CTRACE((tfp,"new number=%d\n",number));
		/*
		 *  Check for a 'p' suffix. - FM
		 */
		if (c == 'p') {
		    /*
		     *  Treat 1 or less as the first page. - FM
		     */
		    if (number <= 1) {
			if (window_offset == 0) {
			    HTUserMsg(ALREADY_AT_OPTION_BEGIN);
			    if (disabled) {
				_statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
			    } else {
				_statusline(FORM_LINK_OPTION_LIST_MESSAGE);
			    }
			    break;
			}
			window_offset = 0;
			cur_selection = 0;
			if (disabled) {
			    _statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
			} else {
			    _statusline(FORM_LINK_OPTION_LIST_MESSAGE);
			}
			goto redraw;
		    }

		    /*
		     *  Treat a number equal to or greater than the
		     *  number of pages as the last page. - FM
		     */
		    if (number >= npages) {
			if (window_offset >= ((num_options - length) + 1)) {
			    HTUserMsg(ALREADY_AT_OPTION_END);
			    if (disabled) {
				_statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
			    } else {
				_statusline(FORM_LINK_OPTION_LIST_MESSAGE);
			    }
			    break;
			}
			window_offset = ((npages - 1) * length);
			if (window_offset > (num_options - length)) {
			    window_offset = (num_options - length + 1);
			}
			if (cur_selection < window_offset)
			    cur_selection = window_offset;
			if (disabled) {
			    _statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
			} else {
			    _statusline(FORM_LINK_OPTION_LIST_MESSAGE);
			}
			goto redraw;
		    }

		    /*
		     *  We want an intermediate page. - FM
		     */
		    if (((number - 1) * length) == window_offset) {
			char *msg = 0;
			HTSprintf0(&msg, ALREADY_AT_OPTION_PAGE, number);
			HTUserMsg(msg);
			FREE(msg);
			if (disabled) {
			    _statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
			} else {
			    _statusline(FORM_LINK_OPTION_LIST_MESSAGE);
			}
			break;
		    }
		    cur_selection = window_offset = ((number - 1) * length);
		    if (disabled) {
			_statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
		    } else {
			_statusline(FORM_LINK_OPTION_LIST_MESSAGE);
		    }
		    goto redraw;

		}

		/*
		 *  Check for a positive number, which signifies
		 *  that an option should be sought. - FM
		 */
		if (number > 0) {
		    /*
		     *  Decrement the number so as to correspond
		     *  with our cur_selection values. - FM
		     */
		    number--;

		    /*
		     *  If the number is in range and had no legal
		     *  suffix, select the indicated option. - FM
		     */
		    if (number <= num_options && c == '\0') {
			cur_selection = number;
			cmd = LYK_ACTIVATE;
			break;
		    }

		    /*
		     *  Verify that we had a 'g' suffix,
		     *  and act on the number. - FM
		     */
		    if (c == 'g') {
			if (cur_selection == number) {
			    /*
			     *  The option already is current. - FM
			     */
			    char *msg = 0;
			    HTSprintf0(&msg, OPTION_ALREADY_CURRENT, (number + 1));
			    HTUserMsg(msg);
			    FREE(msg);
			    if (disabled) {
				_statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
			    } else {
				_statusline(FORM_LINK_OPTION_LIST_MESSAGE);
			    }
			    break;
			}

			if (number <= num_options) {
			    /*
			     *  The number is in range and had a 'g'
			     *  suffix, so make it the current option,
			     *  scrolling if needed. - FM
			     */
			    j = (number - cur_selection);
			    cur_selection = number;
			    if ((j > 0) &&
				(cur_selection - window_offset) >= length) {
				window_offset += j;
				if (window_offset > (num_options - length + 1))
				    window_offset = (num_options - length + 1);
			    } else if ((cur_selection - window_offset) < 0) {
				window_offset -= abs(j);
				if (window_offset < 0)
				    window_offset = 0;
			    }
			    if (disabled) {
				_statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
			    } else {
				_statusline(FORM_LINK_OPTION_LIST_MESSAGE);
			    }
			    goto redraw;
			}

			/*
			 *  Not in range. - FM
			 */
			HTUserMsg(BAD_OPTION_NUM_ENTERED);
		    }
		}

		/*
		 *  Restore the popup statusline. - FM
		 */
		if (disabled) {
		    _statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
		} else {
		    _statusline(FORM_LINK_OPTION_LIST_MESSAGE);
		}
		break;

	    case LYK_PREV_LINK:
	    case LYK_LPOS_PREV_LINK:
	    case LYK_FASTBACKW_LINK:
	    case LYK_UP_LINK:

		if (cur_selection > 0)
		    cur_selection--;

		/*
		 *  Scroll the window up if necessary.
		 */
		if ((cur_selection - window_offset) < 0) {
		    window_offset--;
		    goto redraw;
		}
		break;

	    case LYK_NEXT_LINK:
	    case LYK_LPOS_NEXT_LINK:
	    case LYK_FASTFORW_LINK:
	    case LYK_DOWN_LINK:
		if (cur_selection < num_options)
		    cur_selection++;

		/*
		 *  Scroll the window down if necessary
		 */
		if ((cur_selection - window_offset) >= length) {
		    window_offset++;
		    goto redraw;
		}
		break;

	    case LYK_NEXT_PAGE:
		/*
		 *  Okay, are we on the last page of the list?
		 *  If not then,
		 */
		if (window_offset != (num_options - length + 1)) {
		    /*
		     *  Modify the current selection to not be a
		     *  coordinate in the list, but a coordinate
		     *  on the item selected in the window.
		     */
		    cur_selection -= window_offset;

		    /*
		     *  Page down the proper length for the list.
		     *  If simply to far, back up.
		     */
		    window_offset += length;
		    if (window_offset > (num_options - length)) {
			window_offset = (num_options - length + 1);
		    }

		    /*
		     *  Readjust the current selection to be a
		     *  list coordinate rather than window.
		     *  Redraw this thing.
		     */
		    cur_selection += window_offset;
		    goto redraw;
		}
		else if (cur_selection < num_options) {
		    /*
		     *  Already on last page of the list so just
		     *  redraw it with the last item selected.
		     */
		    cur_selection = num_options;
		}
		break;

	    case LYK_PREV_PAGE:
		/*
		 *  Are we on the first page of the list?
		 *  If not then,
		 */
		if (window_offset != 0) {
		    /*
		     *  Modify the current selection to not be a
		     *  list coordinate, but a window coordinate.
		     */
		    cur_selection -= window_offset;

		    /*
		     *  Page up the proper length.
		     *  If too far, back up.
		     */
		    window_offset -= length;
		    if (window_offset < 0) {
			window_offset = 0;
		    }

		    /*
		     *  Readjust the current selection.
		     */
		    cur_selection += window_offset;
		    goto redraw;
		} else if (cur_selection > 0) {
		    /*
		     *  Already on the first page so just
		     *  back up to the first item.
		     */
		    cur_selection = 0;
		}
		break;

	    case LYK_HOME:
		cur_selection = 0;
		if (window_offset > 0) {
		    window_offset = 0;
		    goto redraw;
		}
		break;

	    case LYK_END:
		cur_selection = num_options;
		if (window_offset != (num_options - length + 1)) {
		    window_offset = (num_options - length + 1);
		    goto redraw;
		}
		break;

	    case LYK_DOWN_TWO:
		cur_selection += 2;
		if (cur_selection > num_options)
		    cur_selection = num_options;

		/*
		 *  Scroll the window down if necessary.
		 */
		if ((cur_selection - window_offset) >= length) {
		    window_offset += 2;
		    if (window_offset > (num_options - length + 1))
			window_offset = (num_options - length + 1);
		    goto redraw;
		}
		break;

	    case LYK_UP_TWO:
		cur_selection -= 2;
		if (cur_selection < 0)
		    cur_selection = 0;

		/*
		 *  Scroll the window up if necessary.
		 */
		if ((cur_selection - window_offset) < 0) {
		    window_offset -= 2;
		    if (window_offset < 0)
			window_offset = 0;
		    goto redraw;
		}
		break;

	    case LYK_DOWN_HALF:
		cur_selection += (length/2);
		if (cur_selection > num_options)
		    cur_selection = num_options;

		/*
		 *  Scroll the window down if necessary.
		 */
		if ((cur_selection - window_offset) >= length) {
		    window_offset += (length/2);
		    if (window_offset > (num_options - length + 1))
			window_offset = (num_options - length + 1);
		    goto redraw;
		}
		break;

	    case LYK_UP_HALF:
		cur_selection -= (length/2);
		if (cur_selection < 0)
		    cur_selection = 0;

		/*
		 *  Scroll the window up if necessary.
		 */
		if ((cur_selection - window_offset) < 0) {
		    window_offset -= (length/2);
		    if (window_offset < 0)
			window_offset = 0;
		    goto redraw;
		}
		break;

	    case LYK_REFRESH:
		lynx_force_repaint();
		refresh();
		break;

	    case LYK_NEXT:
		if (recall && *prev_target_buffer == '\0') {
		    /*
		     *  We got a 'n'ext command with no prior query
		     *  specified within the popup window.  See if
		     *  one was entered when the popup was retracted,
		     *  and if so, assume that's what's wanted.  Note
		     *  that it will become the default within popups,
		     *  unless another is entered within a popup.  If
		     *  the within popup default is to be changed at
		     *  that point, use WHEREIS ('/') and enter it,
		     *  or the up- or down-arrow keys to seek any of
		     *  the previously entered queries, regardless of
		     *  whether they were entered within or outside
		     *  of a popup window. - FM
		     */
		    if ((cp = (char *)HTList_objectAt(search_queries,
						      0)) != NULL) {
			LYstrncpy(prev_target_buffer, cp, sizeof(prev_target_buffer));
			QueryNum = 0;
			FirstRecall = FALSE;
		    }
		}
		strcpy(prev_target, prev_target_buffer);
		/* FALLTHRU */
	    case LYK_WHEREIS:
		if (*prev_target == '\0' ) {
		    _statusline(ENTER_WHEREIS_QUERY);
		    if ((ch = LYgetstr(prev_target, VISIBLE,
				       sizeof(prev_target_buffer),
				       recall)) < 0) {
			/*
			 *  User cancelled the search via ^G. - FM
			 */
			HTInfoMsg(CANCELLED);
			goto restore_popup_statusline;
		    }
		}

check_recall:
		if (*prev_target == '\0' &&
		    !(recall && (ch == UPARROW || ch == DNARROW))) {
		    /*
		     *  No entry.  Simply break.   - FM
		     */
		    HTInfoMsg(CANCELLED);
		    goto restore_popup_statusline;
		}

		if (recall && ch == UPARROW) {
		    if (FirstRecall) {
			/*
			 *  Use the current string or
			 *  last query in the list. - FM
			 */
			FirstRecall = FALSE;
			if (*prev_target_buffer) {
			    for (QueryNum = (QueryTotal - 1);
				 QueryNum > 0; QueryNum--) {
				if ((cp = (char *)HTList_objectAt(
							search_queries,
							QueryNum)) != NULL &&
				    !strcmp(prev_target_buffer, cp)) {
				    break;
				}
			    }
			} else {
			    QueryNum = 0;
			}
		    } else {
			/*
			 *  Go back to the previous query in the list. - FM
			 */
			QueryNum++;
		    }
		    if (QueryNum >= QueryTotal) {
			/*
			 *  Roll around to the last query in the list. - FM
			 */
			QueryNum = 0;
		    }
		    if ((cp = (char *)HTList_objectAt(search_queries,
						      QueryNum)) != NULL) {
			LYstrncpy(prev_target, cp, sizeof(prev_target)-1);
			if (*prev_target_buffer &&
			    !strcmp(prev_target_buffer, prev_target)) {
			    _statusline(EDIT_CURRENT_QUERY);
			} else if ((*prev_target_buffer && QueryTotal == 2) ||
				   (!(*prev_target_buffer) &&
				      QueryTotal == 1)) {
			    _statusline(EDIT_THE_PREV_QUERY);
			} else {
			    _statusline(EDIT_A_PREV_QUERY);
			}
			if ((ch = LYgetstr(prev_target, VISIBLE,
				sizeof(prev_target_buffer), recall)) < 0) {
			    /*
			     *  User cancelled the search via ^G. - FM
			     */
			    HTInfoMsg(CANCELLED);
			    goto restore_popup_statusline;
			}
			goto check_recall;
		    }
		} else if (recall && ch == DNARROW) {
		    if (FirstRecall) {
			/*
			 *  Use the current string or
			 *  first query in the list. - FM
			 */
			FirstRecall = FALSE;
			if (*prev_target_buffer) {
			    for (QueryNum = 0;
				 QueryNum < (QueryTotal - 1); QueryNum++) {
				if ((cp = (char *)HTList_objectAt(
							    search_queries,
							    QueryNum)) != NULL &&
				    !strcmp(prev_target_buffer, cp)) {
					break;
				}
			    }
			} else {
			    QueryNum = (QueryTotal - 1);
			}
		    } else {
			/*
			 *  Advance to the next query in the list. - FM
			 */
			QueryNum--;
		    }
		    if (QueryNum < 0) {
			/*
			 *  Roll around to the first query in the list. - FM
			 */
			QueryNum = (QueryTotal - 1);
		    }
		    if ((cp = (char *)HTList_objectAt(search_queries,
						      QueryNum)) != NULL) {
			LYstrncpy(prev_target, cp, sizeof(prev_target)-1);
			if (*prev_target_buffer &&
			    !strcmp(prev_target_buffer, prev_target)) {
			    _statusline(EDIT_CURRENT_QUERY);
			} else if ((*prev_target_buffer &&
				    QueryTotal == 2) ||
				   (!(*prev_target_buffer) &&
				    QueryTotal == 1)) {
			    _statusline(EDIT_THE_PREV_QUERY);
			} else {
			    _statusline(EDIT_A_PREV_QUERY);
			}
			if ((ch = LYgetstr(prev_target, VISIBLE,
					   sizeof(prev_target_buffer),
					   recall)) < 0) {
			    /*
			     * User cancelled the search via ^G. - FM
			     */
			    HTInfoMsg(CANCELLED);
			    goto restore_popup_statusline;
			}
			goto check_recall;
		    }
		}
		/*
		 *  Replace the search string buffer with the new target. - FM
		 */
		strcpy(prev_target_buffer, prev_target);
		HTAddSearchQuery(prev_target_buffer);

		/*
		 *  Start search at the next option. - FM
		 */
		for (j = 1, tmp_ptr = opt_ptr->next;
		     tmp_ptr != NULL; tmp_ptr = tmp_ptr->next, j++) {
		    if (case_sensitive) {
			if (strstr(tmp_ptr->name, prev_target_buffer) != NULL)
			    break;
		    } else {
			if (LYstrstr(tmp_ptr->name, prev_target_buffer) != NULL)
			    break;
		    }
		}
		if (tmp_ptr != NULL) {
		    /*
		     *  We have a hit, so make that option the current. - FM
		     */
		    cur_selection += j;
		    /*
		     *  Scroll the window down if necessary.
		     */
		    if ((cur_selection - window_offset) >= length) {
			window_offset += j;
			if (window_offset > (num_options - length + 1))
			    window_offset = (num_options - length + 1);
			ReDraw = TRUE;
		    }
		    goto restore_popup_statusline;
		}

		/*
		 *  If we started at the beginning, it can't be present. - FM
		 */
		if (cur_selection == 0) {
		    HTUserMsg2(STRING_NOT_FOUND, prev_target_buffer);
		    goto restore_popup_statusline;
		}

		/*
		 *  Search from the beginning to the current option. - FM
		 */
		for (j = 0, tmp_ptr = list;
		     j < cur_selection; tmp_ptr = tmp_ptr->next, j++) {
		    if (case_sensitive) {
			if (strstr(tmp_ptr->name, prev_target_buffer) != NULL)
			    break;
		    } else {
			if (LYstrstr(tmp_ptr->name, prev_target_buffer) != NULL)
			    break;
		    }
		}
		if (j < cur_selection) {
		    /*
		     *  We have a hit, so make that option the current. - FM
		     */
		    j = (cur_selection - j);
		    cur_selection -= j;
		    /*
		     *  Scroll the window up if necessary.
		     */
		    if ((cur_selection - window_offset) < 0) {
			window_offset -= j;
			if (window_offset < 0)
			    window_offset = 0;
			ReDraw = TRUE;
		    }
		    goto restore_popup_statusline;
		}

		/*
		 *  Didn't find it in the preceding options either. - FM
		 */
		HTUserMsg2(STRING_NOT_FOUND, prev_target_buffer);

restore_popup_statusline:
		/*
		 *  Restore the popup statusline and
		 *  reset the search variables. - FM
		 */
		if (disabled)
		    _statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
		else
		    _statusline(FORM_LINK_OPTION_LIST_MESSAGE);
		*prev_target = '\0';
		QueryTotal = (search_queries ? HTList_count(search_queries)
					     : 0);
		recall = ((QueryTotal >= 1) ? RECALL : NORECALL);
		QueryNum = QueryTotal;
		if (ReDraw == TRUE) {
		    ReDraw = FALSE;
		    goto redraw;
		}
		break;

	    case LYK_QUIT:
	    case LYK_ABORT:
	    case LYK_PREV_DOC:
		cur_selection = orig_selection;
		cmd = LYK_ACTIVATE; /* to exit */
		break;
	}
    }
    LYstopPopup();

    return(disabled ? orig_selection : cur_selection);
}

/*
 *  Display statusline info tailored for the current form field.
 */
PUBLIC void show_formlink_statusline ARGS2(
    CONST FormInfo *,	form,
    int,		for_what)
{
    switch(form->type) {
    case F_PASSWORD_TYPE:
	if (form->disabled == YES)
	    statusline(FORM_LINK_PASSWORD_UNM_MSG);
	else
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
	    if (for_what == FOR_PANEL)
		statusline(FORM_LINK_PASSWORD_MESSAGE_INA);
	    else
#endif
	    statusline(FORM_LINK_PASSWORD_MESSAGE);
	break;
    case F_OPTION_LIST_TYPE:
	if (form->disabled == YES)
	    statusline(FORM_LINK_OPTION_LIST_UNM_MSG);
	else
	    statusline(FORM_LINK_OPTION_LIST_MESSAGE);
	break;
    case F_CHECKBOX_TYPE:
	if (form->disabled == YES)
	    statusline(FORM_LINK_CHECKBOX_UNM_MSG);
	else
	    statusline(FORM_LINK_CHECKBOX_MESSAGE);
	break;
    case F_RADIO_TYPE:
	if (form->disabled == YES)
	    statusline(FORM_LINK_RADIO_UNM_MSG);
	else
	    statusline(FORM_LINK_RADIO_MESSAGE);
	break;
    case F_TEXT_SUBMIT_TYPE:
	if (form->disabled == YES) {
	    statusline(FORM_LINK_TEXT_SUBMIT_UNM_MSG);
	} else if (form->submit_method ==
		   URL_MAIL_METHOD) {
	    if (no_mail)
		statusline(FORM_LINK_TEXT_SUBMIT_MAILTO_DIS_MSG);
	    else
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
		if (for_what == FOR_PANEL)
		    statusline(FORM_TEXT_SUBMIT_MAILTO_MSG_INA);
		else
#endif
		statusline(FORM_LINK_TEXT_SUBMIT_MAILTO_MSG);
	} else if (form->no_cache) {
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
	    if (for_what == FOR_PANEL)
		statusline(FORM_TEXT_RESUBMIT_MESSAGE_INA);
	    else
#endif
	    statusline(FORM_LINK_TEXT_RESUBMIT_MESSAGE);
	} else {
	    char *submit_str = NULL;
	    char *xkey_info = key_for_func_ext(LYK_NOCACHE, for_what);
	    if (xkey_info && *xkey_info) {
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
		if (for_what == FOR_PANEL)
		    HTSprintf0(&submit_str, FORM_TEXT_SUBMIT_MESSAGE_INA_X,
			       xkey_info);
		else
#endif
		    HTSprintf0(&submit_str, FORM_LINK_TEXT_SUBMIT_MESSAGE_X,
			       xkey_info);
		statusline(submit_str);
		FREE(submit_str);
	    } else {
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
		if (for_what == FOR_PANEL)
		    statusline(FORM_LINK_TEXT_SUBMIT_MESSAGE_INA);
		else
#endif
		statusline(FORM_LINK_TEXT_SUBMIT_MESSAGE);
	    }
	    FREE(xkey_info);
	}
	break;
    case F_SUBMIT_TYPE:
    case F_IMAGE_SUBMIT_TYPE:
	if (form->disabled == YES) {
	    statusline(FORM_LINK_SUBMIT_DIS_MSG);
	} else if (form->submit_method ==
		   URL_MAIL_METHOD) {
	    if (no_mail) {
		statusline(FORM_LINK_SUBMIT_MAILTO_DIS_MSG);
	    } else {
		if(user_mode == ADVANCED_MODE) {
		    char *submit_str = NULL;

		    StrAllocCopy(submit_str, FORM_LINK_SUBMIT_MAILTO_PREFIX);
		    StrAllocCat(submit_str, form->submit_action);
		    statusline(submit_str);
		    FREE(submit_str);
		} else {
		    statusline(FORM_LINK_SUBMIT_MAILTO_MSG);
		}
	    }
	} else if (form->no_cache) {
	    if(user_mode == ADVANCED_MODE) {
		char *submit_str = NULL;

		StrAllocCopy(submit_str, FORM_LINK_RESUBMIT_PREFIX);
		StrAllocCat(submit_str, form->submit_action);
		statusline(submit_str);
		FREE(submit_str);
	    } else {
		statusline(FORM_LINK_RESUBMIT_MESSAGE);
	    }
	} else {
	    if(user_mode == ADVANCED_MODE) {
		char *submit_str = NULL;

		StrAllocCopy(submit_str, FORM_LINK_SUBMIT_PREFIX);
		StrAllocCat(submit_str, form->submit_action);
		statusline(submit_str);
		FREE(submit_str);
	    } else {
		statusline(FORM_LINK_SUBMIT_MESSAGE);
	    }
	}
	break;
    case F_RESET_TYPE:
	if (form->disabled == YES)
	    statusline(FORM_LINK_RESET_DIS_MSG);
	else
	    statusline(FORM_LINK_RESET_MESSAGE);
	break;
    case F_FILE_TYPE:
	if (form->disabled == YES)
	    statusline(FORM_LINK_FILE_UNM_MSG);
	else
	    statusline(FORM_LINK_FILE_MESSAGE);
	break;
    case F_TEXT_TYPE:
	if (form->disabled == YES)
	    statusline(FORM_LINK_TEXT_UNM_MSG);
	else
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
	    if (for_what == FOR_PANEL)
		statusline(FORM_LINK_TEXT_MESSAGE_INA);
	    else
#endif
	    statusline(FORM_LINK_TEXT_MESSAGE);
	break;
    case F_TEXTAREA_TYPE:
	if (form->disabled == YES) {
	    statusline(FORM_LINK_TEXT_UNM_MSG);
	} else {
	    char *submit_str = NULL;
	    char *xkey_info = NULL;
	    if (!no_editor && editor && editor) {
		xkey_info = key_for_func_ext(LYK_EDIT_TEXTAREA, for_what);
#ifdef TEXTAREA_AUTOEXTEDIT
		if (!xkey_info)
		    xkey_info = key_for_func_ext(LYK_DWIMEDIT, for_what);
#endif
	    }
	    if (xkey_info && *xkey_info) {
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
		if (for_what == FOR_PANEL)
		    HTSprintf0(&submit_str, FORM_LINK_TEXTAREA_MESSAGE_INA_E,
			       xkey_info);
		else
#endif
		    HTSprintf0(&submit_str, FORM_LINK_TEXTAREA_MESSAGE_E,
			       xkey_info);
		statusline(submit_str);
		FREE(submit_str);
	    } else {
#ifdef TEXTFIELDS_MAY_NEED_ACTIVATION
		if (for_what == FOR_PANEL)
		    statusline(FORM_LINK_TEXTAREA_MESSAGE_INA);
		else
#endif
		    statusline(FORM_LINK_TEXTAREA_MESSAGE);
	    }
	    FREE(xkey_info);
	}
	break;
    }
}
