#ifndef TRSTABLE_H
#define TRSTABLE_H

#include <HTUtils.h>

/* TRST_MAXCOLSPAN and TRST_MAXCOLSPAN are defined in userdefs.h */

typedef struct _STable_info STable_info;
extern STable_info * Stbl_startTABLE PARAMS((short));
extern int Stbl_finishTABLE PARAMS((STable_info *));
extern void Stbl_free PARAMS((STable_info *));
extern int Stbl_addRowToTable PARAMS((STable_info *, short, int));
extern int Stbl_addCellToTable PARAMS((STable_info *, int, int, short, BOOL, int, int));
extern int Stbl_finishCellInTable PARAMS((STable_info *, BOOL, int, int));
extern int Stbl_addColInfo PARAMS((STable_info *, int, short, BOOL));
extern int Stbl_finishColGroup PARAMS((STable_info *));
extern int Stbl_addRowGroup PARAMS((STable_info *, short));
#define Stbl_lineBreak(stbl,l,pos) Stbl_finishCellInTable(stbl, NO, l, pos)
extern int Stbl_getStartLine PARAMS((STable_info *));
extern int Stbl_getFixupPositions PARAMS((
    STable_info *	me,
    int			lineno,
    int *		oldpos,
    int *		newpos));
extern short Stbl_getAlignment PARAMS((STable_info *));

#ifdef EXP_NESTED_TABLES
extern void Stbl_update_enclosing PARAMS((
    STable_info *	me,
    int			max_width,
    int			last_lineno));
struct _TextAnchor;
extern void Stbl_set_enclosing PARAMS(( STable_info *me,
					STable_info *encl,
					struct _TextAnchor *last_anchor));
extern STable_info * Stbl_get_enclosing PARAMS((STable_info *	me));
extern struct _TextAnchor * Stbl_get_last_anchor_before PARAMS((STable_info *	me));
#endif

#endif /* TRSTABLE_H */
