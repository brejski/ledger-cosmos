/*******************************************************************************
*   (c) 2016 Ledger
*   (c) 2018 ZondaX GmbH
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include "view.h"
#include "view_templates.h"
#include "common.h"

#include "glyphs.h"
#include "bagl.h"

#include <string.h>
#include <stdio.h>

#define TRUE  1
#define FALSE 0

ux_state_t ux;
enum UI_STATE view_uiState;

void update_transaction_page_info();
void display_transaction_page(int);

volatile char transactionDataKey[MAX_CHARS_PER_KEY_LINE];
volatile char transactionDataValue[MAX_CHARS_PER_VALUE_LINE];
volatile char pageInfo[MAX_SCREEN_LINE_WIDTH];

int value_scrolling_mode;

// Index of the currently displayed page
int transactionDetailsCurrentPage;
// Total number of displayable pages
int transactionDetailsPageCount;

// Below data is used to help split long messages that are scrolled
// into smaller chunks so they fit the memory buffer
// Index of currently displayed value chunk
int transactionChunksPageIndex;
// Total number of displayable value chunks
int transactionChunksPageCount;

void start_transaction_info_display(unsigned int unused);

void view_sign_transaction(unsigned int unused);

void reject(unsigned int unused);

//------ View elements
const ux_menu_entry_t menu_main[];
const ux_menu_entry_t menu_about[];

const ux_menu_entry_t menu_transaction_info[] = {
        {NULL, start_transaction_info_display, 0, NULL, "View transaction", NULL, 0, 0},
        {NULL, view_sign_transaction, 0, NULL, "Sign transaction", NULL, 0, 0},
        {NULL, reject, 0, &C_icon_back, "Reject", NULL, 60, 40},
        UX_MENU_END
};

const ux_menu_entry_t menu_main[] = {
#ifdef TESTING_ENABLED
        {NULL, NULL, 0, &C_icon_app, "Tendermint", "Cosmos TEST!", 33, 12},
#else
        {NULL, NULL, 0, &C_icon_app, "Tendermint", "Cosmos", 33, 12},
#endif
        {menu_about, NULL, 0, NULL, "About", NULL, 0, 0},
        {NULL, os_sched_exit, 0, &C_icon_dashboard, "Quit app", NULL, 50, 29},
        UX_MENU_END
};

const ux_menu_entry_t menu_about[] = {
        {NULL, NULL, 0, NULL, "Version", APPVERSION, 0, 0},
        {menu_main, NULL, 2, &C_icon_back, "Back", NULL, 61, 40},
        UX_MENU_END
};

static const bagl_element_t bagl_ui_sign_transaction[] = {
    UI_FillRectangle(0, 0, 0, 128, 32, 0x000000, 0xFFFFFF),
    UI_Icon(0, 3, 32 / 2 - 4, 7, 7, BAGL_GLYPH_ICON_CROSS),
    UI_LabelLineNoScrolling(1, 0, 12, 128, 11, 0xFFFFFF, 0x000000, "Sign transaction"),
    UI_LabelLineNoScrolling(1, 0, 23, 128, 11, 0xFFFFFF, 0x000000, "Not implemented yet"),
};

static const bagl_element_t bagl_ui_transaction_info_valuescrolling[] = {
    UI_FillRectangle(0, 0, 0, 128, 32, 0x000000, 0xFFFFFF),
    UI_Icon(0, 0, 0, 7, 7, BAGL_GLYPH_ICON_LEFT),
    UI_Icon(0, 128-7, 0, 7, 7, BAGL_GLYPH_ICON_RIGHT),
    UI_LabelLineNoScrolling(1, 0, 8, 128, 11, 0xFFFFFF, 0x000000,(const char*)pageInfo),
    UI_LabelLineNoScrolling(1, 0, 19, 128, 11, 0xFFFFFF, 0x000000,(const char*)transactionDataKey),
    UI_LabelLine(2, 0, 30, 128, 11, 0xFFFFFF, 0x000000,(const char*)transactionDataValue),
};

static const bagl_element_t bagl_ui_transaction_info_keyscrolling[] = {
    UI_FillRectangle(0, 0, 0, 128, 32, 0x000000, 0xFFFFFF),
    UI_Icon(0, 0, 0, 7, 7, BAGL_GLYPH_ICON_LEFT),
    UI_Icon(0, 128-7, 0, 7, 7, BAGL_GLYPH_ICON_RIGHT),
    UI_LabelLineNoScrolling(1, 0, 8, 128, 11, 0xFFFFFF, 0x000000,(const char*)pageInfo),
    UI_LabelLine(3, 0, 19, 128, 11, 0xFFFFFF, 0x000000,(const char*)transactionDataKey),
};
//------ View elements

//------ Event handlers
delegate_update_transaction_info event_handler_update_transaction_info = NULL;
delegate_reject_transaction event_handler_reject_transaction = NULL;
delegate_sign_transaction event_handler_sign_transaction = NULL;

void view_add_update_transaction_info_event_handler(delegate_update_transaction_info delegate) {
    event_handler_update_transaction_info = delegate;
}

void view_add_reject_transaction_event_handler(delegate_reject_transaction delegate) {
    event_handler_reject_transaction = delegate;
}

void view_add_sign_transaction_event_handler(delegate_sign_transaction delegate) {
    event_handler_sign_transaction = delegate;
}
// ------ Event handlers

static unsigned int bagl_ui_sign_transaction_button(unsigned int button_mask,
                                                    unsigned int button_mask_counter) {
    switch (button_mask) {
        default:
            view_display_transaction_menu(0);
    }
    return 0;
}

const bagl_element_t *ui_transaction_info_prepro(const bagl_element_t *element) {

    switch (element->component.userid) {
        case 0x01:
            UX_CALLBACK_SET_INTERVAL(2000);
            break;
        case 0x02:
            UX_CALLBACK_SET_INTERVAL(MAX(3000, 1000 + bagl_label_roundtrip_duration_ms(element, 7)));
            break;
        case 0x03:
            UX_CALLBACK_SET_INTERVAL(MAX(3000, 1000 + bagl_label_roundtrip_duration_ms(element, 7)));
            break;
    }
    return element;
}

void submenu_left()
{
    transactionChunksPageIndex--;
    display_transaction_page(TRUE);
}

void submenu_right()
{
    transactionChunksPageIndex++;
    display_transaction_page(TRUE);
}

void menu_left()
{
    transactionChunksPageIndex = 0;
    transactionChunksPageCount = 1;
    if (transactionDetailsCurrentPage > 0) {
        transactionDetailsCurrentPage--;
        display_transaction_page(TRUE);
    } else {
        view_display_transaction_menu(0);
    }
}

void menu_right()
{
    transactionChunksPageIndex = 0;
    transactionChunksPageCount = 1;
    if (transactionDetailsCurrentPage < transactionDetailsPageCount - 1) {
        transactionDetailsCurrentPage++;
        display_transaction_page(TRUE);
    } else {
        view_display_transaction_menu(0);
    }
}

static unsigned int bagl_ui_transaction_info_valuescrolling_button(
        unsigned int button_mask,
        unsigned int button_mask_counter)
{
    switch (button_mask) {
        // Hold left and right long to quit
        case BUTTON_EVT_RELEASED | BUTTON_LEFT | BUTTON_RIGHT: {
            view_display_transaction_menu(0);
            break;
        }

            // Press to progress to the previous element
        case BUTTON_EVT_RELEASED | BUTTON_LEFT: {
            if (transactionChunksPageIndex > 0) {
                submenu_left();
            } else {
                menu_left();
            }
            break;
        }

            // Hold to progress to the previous element quickly
            // It also steps out from the value chunk page view mode
        case BUTTON_EVT_FAST | BUTTON_LEFT: {
            menu_left();
            break;
        }

            // Press to progress to the next element
        case BUTTON_EVT_RELEASED | BUTTON_RIGHT: {
            if (transactionChunksPageIndex < transactionChunksPageCount - 1) {
                submenu_right();
            } else {
                menu_right();
            }
            break;
        }

            // Hold to progress to the next element quickly
            // It also steps out from the value chunk page view mode
        case BUTTON_EVT_FAST | BUTTON_RIGHT: {
            menu_right();
            break;
        }
    }
    return 0;
}

void switch_to_value_scrolling()
{
    if (value_scrolling_mode == FALSE) {
        int offset = strlen((char*)transactionDataKey) - MAX_SCREEN_LINE_WIDTH;
        if (offset > 0) {
            char* start = (char*)transactionDataKey;
            while (1) {
                *start = start[offset];
                if (*start == '\0') break;
                start++;
            }
        }
        value_scrolling_mode = TRUE;
        display_transaction_page(FALSE);
    }
}

static unsigned int bagl_ui_transaction_info_keyscrolling_button(
        unsigned int button_mask,
        unsigned int button_mask_counter)
{
    switch (button_mask) {
        // Hold left and right long to quit
        case BUTTON_EVT_RELEASED | BUTTON_LEFT | BUTTON_RIGHT: {
            view_display_transaction_menu(0);
            break;
        }

        // Press to progress to the previous element
        case BUTTON_EVT_RELEASED | BUTTON_LEFT:
        // Press to progress to the next element
        case BUTTON_EVT_RELEASED | BUTTON_RIGHT: {
            switch_to_value_scrolling();
        } break;
    }
    return 0;
}


void display_transaction_page(int update)
{
    if (update == TRUE) {
        update_transaction_page_info();
    }
    if (value_scrolling_mode == TRUE) {
        UX_DISPLAY(bagl_ui_transaction_info_valuescrolling, ui_transaction_info_prepro);
    }
    else
    {
        UX_DISPLAY(bagl_ui_transaction_info_keyscrolling, ui_transaction_info_prepro);
    }
}


void start_transaction_info_display(unsigned int unused)
{
    value_scrolling_mode = TRUE;
    transactionDetailsCurrentPage = 0;
    transactionChunksPageIndex = 0;
    transactionChunksPageCount = 1;
    display_transaction_page(TRUE);
}

void update_transaction_page_info() {
    if (event_handler_update_transaction_info != NULL) {

        if (event_handler_update_transaction_info != NULL) {

            int index = transactionChunksPageIndex;
            event_handler_update_transaction_info(
                    (char *) transactionDataKey,
                    sizeof(transactionDataKey),
                    (char *) transactionDataValue,
                    sizeof(transactionDataValue),
                    transactionDetailsCurrentPage,
                    &index);
            transactionChunksPageCount = index;

            if (transactionChunksPageCount > 1) {
                int position = strlen((char *) transactionDataKey);
                snprintf(
                        (char *) transactionDataKey + position,
                        sizeof(transactionDataKey) - position,
                        " %02d/%02d",
                        transactionChunksPageIndex + 1,
                        transactionChunksPageCount);
            }

            if (strlen((char*)transactionDataKey) > MAX_SCREEN_LINE_WIDTH) {
                value_scrolling_mode = FALSE;
            }
            else {
                value_scrolling_mode = TRUE;
            }
        }

        switch (current_sigtype) {
            case SECP256K1:
                snprintf(
                        (char *) pageInfo,
                        sizeof(pageInfo),
                        "SECP256K1 - %02d/%02d",
                        transactionDetailsCurrentPage + 1,
                        transactionDetailsPageCount);
                break;
#ifdef FEATURE_ED25519
            case ED25519:
                snprintf(
                        (char *) pageInfo,
                        sizeof(pageInfo),
                        "ED25519 - %02d/%02d",
                        transactionDetailsCurrentPage + 1,
                        transactionDetailsPageCount);
                break;
#endif
        }

    }
}

void view_sign_transaction(unsigned int unused) {
    UNUSED(unused);

    if (event_handler_sign_transaction != NULL) {
        event_handler_sign_transaction();
    } else {
        UX_DISPLAY(bagl_ui_sign_transaction, NULL);
    }
}

void reject(unsigned int unused) {
    if (event_handler_reject_transaction != NULL) {
        event_handler_reject_transaction();
    }
}

void io_seproxyhal_display(const bagl_element_t *element) {
    io_seproxyhal_display_default((bagl_element_t *) element);
}

void view_init(void) {
    UX_INIT();
    view_uiState = UI_IDLE;
}

void view_idle(unsigned int ignored) {
    view_uiState = UI_IDLE;
    UX_MENU_DISPLAY(0, menu_main, NULL);
}

void view_display_transaction_menu(unsigned int numberOfTransactionPages) {
    if (numberOfTransactionPages != 0) {
        transactionDetailsPageCount = numberOfTransactionPages;
    }
    view_uiState = UI_TRANSACTION;
    UX_MENU_DISPLAY(0, menu_transaction_info, NULL);
}

void view_display_signing_success() {
    // TODO Add view
    view_idle(0);
}

void view_display_signing_error() {
    // TODO Add view
    view_idle(0);
}