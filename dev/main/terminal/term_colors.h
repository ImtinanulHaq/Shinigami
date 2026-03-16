/**
 * @file    term_colors.h
 * @brief   Color pair definitions for the RED theme in shinigami-terminal.
 *
 * All ncurses color pairs initialized in term_colors_init().
 * Use CLR_* constants everywhere for consistent coloring.
 */
#ifndef TERM_COLORS_H
#define TERM_COLORS_H

#include <ncurses.h>

/* ──────────────────────────────────────────────────────────────────────── */
/* COLOR PAIR INDICES (passed to attron/attroff) */
/* ──────────────────────────────────────────────────────────────────────── */

#define CLR_DEFAULT        1   /**< white on black (default text) */
#define CLR_TITLE          2   /**< bright white + bold (section titles) */
#define CLR_BORDER         3   /**< dark red on black (box borders) */
#define CLR_ACTIVE_TAB     4   /**< black on bright red (selected tab) */
#define CLR_INACTIVE_TAB   5   /**< dim white on black (unselected tab) */
#define CLR_GOOD           6   /**< bright green on black (healthy) */
#define CLR_WARN           7   /**< bright yellow on black (warnings) */
#define CLR_CRIT           8   /**< bright red on black (critical/errors) */
#define CLR_DIM            9   /**< dim white on black (secondary text) */
#define CLR_CMD            10  /**< bright red on black (command input) */
#define CLR_HIGHLIGHT      11  /**< black on bright red (selected row) */
#define CLR_BADGE_OK       12  /**< black on bright green (OK badge) */
#define CLR_BADGE_WARN     13  /**< black on bright yellow (WARN badge) */
#define CLR_BADGE_CRIT     14  /**< white on red (CRITICAL badge) */
#define CLR_BADGE_INFO     15  /**< white on blue (INFO badge) */
#define CLR_TOPBAR         16  /**< bright red on black (top bar) */
#define CLR_BOTTOMBAR      17  /**< dim red on black (bottom bar) */
#define CLR_INPUT          18  /**< white on dark red (command line) */
#define CLR_MONITOR_GOOD   19  /**< bright green on black (monitor healthy) */
#define CLR_MONITOR_GRAPH  20  /**< bright red on black (graph lines) */

/* ──────────────────────────────────────────────────────────────────────── */
/* BOX DRAWING CHARACTERS (UTF-8) */
/* ──────────────────────────────────────────────────────────────────────── */

#define BOX_TL             "╔"   /**< top-left corner */
#define BOX_TR             "╗"   /**< top-right corner */
#define BOX_BL             "╚"   /**< bottom-left corner */
#define BOX_BR             "╝"   /**< bottom-right corner */
#define BOX_H              "═"   /**< horizontal line */
#define BOX_V              "║"   /**< vertical line */
#define BOX_LEFT_T         "╠"   /**< left tee */
#define BOX_RIGHT_T        "╣"   /**< right tee */
#define BOX_TOP_T          "╦"   /**< top tee */
#define BOX_BOTTOM_T       "╩"   /**< bottom tee */
#define BOX_CROSS          "╬"   /**< cross */

/* Light box drawing (for nested/inner boxes) */
#define BOX_TL_LIGHT       "┌"
#define BOX_TR_LIGHT       "┐"
#define BOX_BL_LIGHT       "└"
#define BOX_BR_LIGHT       "┘"
#define BOX_H_LIGHT        "─"
#define BOX_V_LIGHT        "│"

/* ──────────────────────────────────────────────────────────────────────── */
/* BADGE SYMBOLS */
/* ──────────────────────────────────────────────────────────────────────── */

#define BADGE_OK           "✓"    /**< checkmark for OK status */
#define BADGE_FAIL         "✗"    /**< X for failed status */
#define BADGE_WORKING      "●"    /**< bullet for working/active */
#define BADGE_WARN         "⚠"    /**< warning triangle */
#define BADGE_CRIT         "🔴"   /**< red circle for critical */
#define BADGE_LIVE         "●"    /**< connected live */
#define BADGE_DISC         "✗"    /**< disconnected */

/* ──────────────────────────────────────────────────────────────────────── */
/* FUNCTION PROTOTYPES */
/* ──────────────────────────────────────────────────────────────────────── */

/**
 * @brief  Initialize all ncurses color pairs (call once at startup).
 * @return 0 on success, -1 if colors not supported.
 */
int term_colors_init(void);

/**
 * @brief  Apply a specific color pair to the terminal.
 * @param  pair  One of the CLR_* constants.
 */
void term_colors_apply(int pair);

/**
 * @brief  Remove color formatting.
 */
void term_colors_clear(void);

#endif /* TERM_COLORS_H */
