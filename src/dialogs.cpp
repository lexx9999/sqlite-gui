#include"resource.h"
#include"global.h"
#include "prefs.h"
#include "utils.h"
#include"dialogs.h"

namespace dialogs {
	WNDPROC cbOldEditDataEdit, cbOldAddTableCell;
	LRESULT CALLBACK cbNewEditDataEdit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK cbNewAddTableCell(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	const TCHAR* DATATYPES16[] = {TEXT("integer"), TEXT("real"), TEXT("text"), TEXT("null"), TEXT("blob"), TEXT("json"), 0};
	const TCHAR* INDENT_LABELS[] = {TEXT("Tab"), TEXT("2 spaces"), TEXT("4 spaces"), 0};
	const TCHAR* INDENTS[] = {TEXT("\t"), TEXT("  "), TEXT("    ")};

	bool isRequireHighligth = false;
	bool isRequireParenthesisHighligth = false;

	HBITMAP hButtonIcons [] = {
		(HBITMAP)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDB_BTN_ADD), IMAGE_BITMAP, 16, 16, LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS),
		(HBITMAP)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDB_BTN_DELETE), IMAGE_BITMAP, 16, 16, LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS),
		(HBITMAP)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDB_BTN_REFRESH), IMAGE_BITMAP, 16, 16, LR_LOADTRANSPARENT | LR_LOADMAP3DCOLORS)
	};

	BOOL CALLBACK cbDlgAddEdit (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				bool isEdit = lParam == IDM_EDIT;

				TCHAR name16[256] = {0};
				TV_ITEM tv;
				tv.mask = TVIF_TEXT | TVIF_HANDLE | TVIF_PARAM;
				tv.hItem = treeItems[0];
				tv.pszText = name16;
				tv.cchTextMax = 256;

				if(!TreeView_GetItem(hTreeWnd, &tv) || !tv.lParam || tv.lParam == COLUMN)
					return EndDialog(hWnd, -1);

				int type = abs(tv.lParam);
				SetWindowLong(hWnd, GWL_USERDATA, type);

				HWND hDlgEditorWnd = GetDlgItem(hWnd, IDC_DLG_EDITOR);
				TCHAR buf[512];
				_stprintf(buf, isEdit ? TEXT("Edit %s \"%s\"") : TEXT("Add %s"), TYPES16[type], name16);
				SetWindowText(hWnd, buf);

				SendMessage(hDlgEditorWnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_KEYEVENTS);
				setEditorFont(hDlgEditorWnd);

				SetFocus(hDlgEditorWnd);

				if (isEdit) {
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_EXAMPLE), SW_HIDE);
					TCHAR* sql16 = getDDL(name16, type);
					if (sql16) {
						TCHAR buf[_tcslen(sql16) + 128];
						_stprintf(buf, TEXT("drop %s if exists \"%s\";\n\n%s;"), TYPES16[type], name16, sql16);
						SetWindowText(hDlgEditorWnd, buf);
					} else {
						SetWindowText(hDlgEditorWnd, TEXT("Error to get SQL"));
					}
					delete [] sql16;
				}
			}
			break;

			case WM_COMMAND: {
				if (LOWORD(wParam) == IDC_DLG_EDITOR && HIWORD(wParam) == EN_CHANGE && prefs::get("use-highlight") && !isRequireHighligth) {
					PostMessage(hWnd, WMU_HIGHLIGHT, 0, 0);
					isRequireHighligth = true;
				}

				if (wParam == IDC_DLG_EXAMPLE) {
					TCHAR buf[1024];
					int type = GetWindowLong(hWnd, GWL_USERDATA);
					LoadString(GetModuleHandle(NULL), IDS_CREATE_DDL + type, buf, 1024);
					SetWindowText(GetDlgItem(hWnd, IDC_DLG_EDITOR), buf);
				}

				if (wParam == IDC_DLG_OK) {
					HWND hDlgEditorWnd = GetDlgItem(hWnd, IDC_DLG_EDITOR);
					int size = GetWindowTextLength(hDlgEditorWnd) + 1;
					TCHAR* text = new TCHAR[size]{0};
					GetWindowText(hDlgEditorWnd, text, size);
					bool isOk = executeCommandQuery(text);
					delete [] text;

					if (isOk) {
						EndDialog(hWnd, DLG_OK);
					} else {
						SetFocus(hDlgEditorWnd);
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (wParam == IDC_DLG_EDITOR && pHdr->code == EN_SELCHANGE && !isRequireParenthesisHighligth) {
					SELCHANGE *pSc = (SELCHANGE *)lParam;
					if (pSc->seltyp > 0)
						return 1;

					PostMessage(hWnd, WMU_HIGHLIGHT, 0, 0);
					isRequireParenthesisHighligth = true;
				}

				if (wParam == IDC_DLG_EDITOR && pHdr->code == EN_MSGFILTER) {
					return processEditorEvents((MSGFILTER*)lParam);
				}
			}
			break;

			case WMU_HIGHLIGHT: {
				processHightlight(GetDlgItem(hWnd, IDC_DLG_EDITOR), isRequireHighligth, isRequireParenthesisHighligth);
				isRequireHighligth = false;
				isRequireParenthesisHighligth = false;
			}
			break;

			case WM_CLOSE:
				EndDialog(hWnd, DLG_CANCEL);
				break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgAddTable (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_COLUMNS);

				const TCHAR* colNames[] = {TEXT("#"), TEXT("Name"), TEXT("Type"), TEXT("PK"), TEXT("NN"), TEXT("UQ"), TEXT("Default"), TEXT("Check"), 0};
				int colWidths[] = {30, 145, 60, 30, 30, 30, 0, 0, 0};

				for (int i = 0; colNames[i]; i++) {
					LVCOLUMN lvc = {0};
					lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT;
					lvc.iSubItem = i;
					lvc.pszText = (TCHAR*)colNames[i];
					lvc.cchTextMax = _tcslen(colNames[i]) + 1;
					lvc.cx = colWidths[i];
					lvc.fmt = colWidths[i] < 80 ? LVCFMT_CENTER : LVCFMT_LEFT;
					ListView_InsertColumn(hListWnd, i, &lvc);
				}

				LVCOLUMN lvc = {mask: LVCF_FMT, fmt: LVCFMT_RIGHT};
				ListView_SetColumn(hListWnd, 0, &lvc);

				SendMessage(hWnd, WMU_ADD_ROW, 0, 0);
				ListView_SetExtendedListViewStyle(hListWnd, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT);
			}
			break;

			case WM_COMMAND: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_COLUMNS);
				if (wParam == IDC_DLG_OK) {
					int rowCount = ListView_GetItemCount(hListWnd);
					bool isWithoutRowid = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISWITHOUT_ROWID));
					int pkCount = 0;
					TCHAR pk16[1024] = {0};
					for (int i = 0; i < rowCount; i++) {
						TCHAR colName16[255];
						TCHAR isPK[2];
						ListView_GetItemText(hListWnd, i, 1, colName16, 255);
						ListView_GetItemText(hListWnd, i, 3, isPK, 2);
						if (_tcslen(colName16) > 0 && _tcslen(isPK)) {
							if (_tcslen(pk16))
								_tcscat(pk16, TEXT("\",\""));
							_tcscat(pk16, colName16);
							pkCount++;
						}
					}

					int colCount = 0;
					TCHAR columns16[MAX_TEXT_LENGTH] = {0};
					for (int rowNo = 0; rowNo < rowCount; rowNo++) {
						TCHAR* row[8] = {0};
						for (int colNo = 0; colNo < 8; colNo++) {
							row[colNo] = new TCHAR[512]{0};
							ListView_GetItemText(hListWnd, rowNo, colNo, row[colNo], 512);
						}

						if (!_tcslen(row[1]))
							continue;

						TCHAR colDefinition16[2048];
						// name type [NOT NULL] [DEFAULT ...] [CHECK(...)] [PRIMARY KEY] [AUTOINCREMENT] [UNIQUE]
						_stprintf(colDefinition16, TEXT("\"%s\" %s%s%s%s%s%s%s%s%s%s"),
							row[1], // name
							row[2], // type
							_tcslen(row[4]) ? TEXT(" not null") : TEXT(""),
							_tcslen(row[6]) ? TEXT(" default \"") : TEXT(""),
							_tcslen(row[6]) ? row[6] : TEXT(""),
							_tcslen(row[6]) ? TEXT("\"") : TEXT(""),
							_tcslen(row[7]) ? TEXT(" check(") : TEXT(""),
							_tcslen(row[7]) ? row[7] : TEXT(""),
							_tcslen(row[7]) ? TEXT(")") : TEXT(""),
							!_tcslen(row[3]) || pkCount > 1 ? TEXT("") : !_tcscmp(TEXT("integer"), row[2]) && !isWithoutRowid ? TEXT(" primary key autoincrement")	: TEXT(" primary key"),
							_tcslen(row[5]) ? TEXT(" unique") : TEXT("")
							);

						if (_tcslen(columns16))
							_tcscat(columns16, TEXT(",\n"));
						_tcscat(columns16, colDefinition16);

						for (int i = 0; i < 8; i++)
							delete [] row[i];

						colCount++;
					}

					TCHAR tblName16[255] = {0};
					GetDlgItemText(hWnd, IDC_DLG_TABLENAME, tblName16, 255);

					if (!colCount || !_tcslen(tblName16)) {
						MessageBox(hWnd, TEXT("The table should have a name and at least one column"), NULL, 0);
						return 0;
					}

					TCHAR query16[MAX_TEXT_LENGTH] = {0};
					_stprintf(query16, TEXT("create table \"%s\" (\n%s%s%s%s\n)%s"),
						tblName16,
						columns16,
						pkCount > 1 ? TEXT(", primary key(\"") : TEXT(""),
						pkCount > 1 ? pk16 : TEXT(""),
						pkCount > 1 ? TEXT("\")") : TEXT(""),
						isWithoutRowid ? TEXT(" without rowid") : TEXT("")
					);

					if(executeCommandQuery(query16))
						EndDialog(hWnd, DLG_OK);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);

				if (wParam == IDC_DLG_MORE) {
					HWND hBtn = GetDlgItem(hWnd, IDC_DLG_MORE);
					bool isOpen = GetWindowLong(hBtn, GWL_USERDATA);
					SetWindowLong(hBtn, GWL_USERDATA, !isOpen);
					SetWindowText(hBtn, isOpen ? TEXT(">>") : TEXT("<<"));

					RECT rc;
					GetWindowRect(hWnd, &rc);
					SetWindowPos(hWnd, 0, 0, 0, rc.right - rc.left + (isOpen ? -250 : 250), rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

					GetWindowRect(hListWnd, &rc);
					SetWindowPos(hListWnd, 0, 0, 0, rc.right - rc.left + (isOpen ? -250 : 250), rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

					LVCOLUMN lvc = {mask: LVCF_WIDTH, fmt: 0, cx: + (isOpen ? 0 : 125)};
					ListView_SetColumn(hListWnd, 6, &lvc);
					ListView_SetColumn(hListWnd, 7, &lvc);
				}

				if (wParam == IDC_DLG_ROW_ADD)
					SendMessage(hWnd, WMU_ADD_ROW, 0, 0);

				if (wParam == IDC_DLG_ROW_DEL) {
					int pos = ListView_GetNextItem(hListWnd, -1, LVNI_SELECTED);
					if (pos != -1) {
						ListView_DeleteItem(hListWnd, pos);
						SendMessage(hWnd, WMU_UPDATE_ROWNO, 0, 0);
						ListView_SetItemState (hListWnd, pos, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
					}
				}

				if (wParam == IDC_DLG_ROW_UP || wParam == IDC_DLG_ROW_DOWN) {
					int pos = ListView_GetNextItem(hListWnd, -1, LVNI_SELECTED);
					if (pos == -1 || (pos == 0 && wParam == IDC_DLG_ROW_UP) || (pos == ListView_GetItemCount(hListWnd) - 1 && wParam == IDC_DLG_ROW_DOWN))
						return true;

					pos = wParam == IDC_DLG_ROW_UP ? pos - 1 : pos;

					HWND hHeader = ListView_GetHeader(hListWnd);
					for (int i = 0; i < Header_GetItemCount(hHeader); i++) {
						TCHAR buf[255]{0};
						ListView_GetItemText(hListWnd, pos, i, buf, 255);
						LVITEM lvi = {0};
						lvi.mask = LVIF_TEXT;
						lvi.iItem = pos + 2;
						lvi.iSubItem = i;
						lvi.pszText = buf;
						lvi.cchTextMax = 255;
						if (i == 0)
							ListView_InsertItem(hListWnd, &lvi);
						else
							ListView_SetItem(hListWnd, &lvi);
					}
					ListView_DeleteItem(hListWnd, pos);
					if (wParam == IDC_DLG_ROW_DOWN)
						ListView_SetItemState (hListWnd, pos + 1, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);

					SendMessage(hWnd, WMU_UPDATE_ROWNO, 0, 0);
				}
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (pHdr->code == (DWORD)NM_CLICK && pHdr->hwndFrom == GetDlgItem(hWnd, IDC_DLG_COLUMNS)) {
					HWND hListWnd = pHdr->hwndFrom;
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;

					if (ia->iItem == -1)
						return true;

					RECT rect;
					ListView_GetSubItemRect(hListWnd, ia->iItem, ia->iSubItem, LVIR_BOUNDS, &rect);
					int h = rect.bottom - rect.top;
					int w = ListView_GetColumnWidth(hListWnd, ia->iSubItem);

					TCHAR buf[1024];
					ListView_GetItemText(hListWnd, ia->iItem, ia->iSubItem, buf, MAX_TEXT_LENGTH);

					if (ia->iSubItem > 0 && w < 60) {
						ListView_SetItemText(hListWnd, ia->iItem, ia->iSubItem, (TCHAR*)(_tcslen(buf) ? TEXT("") : TEXT("v")));
						return true;
					}

					HWND hCell = 0;
					if (ia->iSubItem == 2) {
						hCell = CreateWindow(WC_COMBOBOX, buf, CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VISIBLE | WS_CHILD | WS_TABSTOP, rect.left, rect.top - 4, w + 18, 200, hListWnd, NULL, GetModuleHandle(0), NULL);
						ComboBox_AddString(hCell, TEXT(""));
						for (int i = 0; DATATYPES16[i]; i++)
							ComboBox_AddString(hCell, DATATYPES16[i]);
						ComboBox_SetCurSel(hCell, 0);
					}

					if (ia->iSubItem == 1 || ia->iSubItem == 6 || ia->iSubItem == 7) {
						hCell = CreateWindowEx(0, WC_EDIT, buf, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, rect.left, rect.top, w, h - 1, hListWnd, 0, GetModuleHandle(NULL), NULL);
						int end = GetWindowTextLength(hCell);
						SendMessage(hCell, EM_SETSEL, end, end);
					}

					SendMessage(hCell, WM_SETFONT, (LPARAM)hDefFont, true);
					SetWindowLong(hCell, GWL_USERDATA, MAKELPARAM(ia->iItem, ia->iSubItem));
					cbOldAddTableCell = (WNDPROC)SetWindowLong(hCell, GWL_WNDPROC, (LONG)cbNewAddTableCell);
					SetFocus(hCell);
				}

				if (wParam == IDC_DLG_EDITOR) {
					NMHDR* pHdr = (LPNMHDR)lParam;
					if (pHdr->code == EN_MSGFILTER)
						return processEditorEvents((MSGFILTER*)lParam);
				}
			}
			break;

			case WMU_ADD_ROW: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_COLUMNS);

				LVITEM lvi = {0};
				lvi.mask = LVIF_TEXT;
				lvi.iSubItem = 0;
				lvi.iItem = ListView_GetItemCount(hListWnd);
				lvi.pszText = (TCHAR*)TEXT("");
				lvi.cchTextMax = 1;
				ListView_InsertItem(hListWnd, &lvi);
				SendMessage(hWnd, WMU_UPDATE_ROWNO, 0, 0);
			}
			break;

			case WMU_UPDATE_ROWNO: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_COLUMNS);
				for (int i = 0; i < ListView_GetItemCount(hListWnd); i++) {
					TCHAR buf[32];
					_itot(i + 1, buf, 10);
					LVITEM lvi = {0};
					lvi.mask = LVIF_TEXT;
					lvi.iSubItem = 0;
					lvi.iItem = i;
					lvi.pszText = buf;
					lvi.cchTextMax = 32;
					ListView_SetItem(hListWnd, &lvi);
				}
			}
			break;

			case WM_CLOSE:
				EndDialog(hWnd, DLG_CANCEL);
				break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgQueryList (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				SetWindowLong(hWnd, GWL_USERDATA, lParam);
				SetWindowPos(hWnd, 0, prefs::get("x") + 40, prefs::get("y") + 80, prefs::get("width") - 80, prefs::get("height") - 120,  SWP_NOZORDER);
				ShowWindow (hWnd, prefs::get("maximized") == 1 ? SW_MAXIMIZE : SW_SHOW);
				SetWindowText(hWnd, lParam == IDM_HISTORY ? TEXT("Query history") : TEXT("Saved queries"));

				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);

				LVCOLUMN lvc;
				lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
				lvc.iSubItem = 0;
				lvc.pszText = (TCHAR*)TEXT("Date");
				lvc.cx = 110;
				ListView_InsertColumn(hListWnd, 0, &lvc);

				lvc.mask = LVCF_TEXT | LVCF_SUBITEM;
				lvc.iSubItem = 1;
				lvc.pszText = (TCHAR*)TEXT("Query");
				ListView_InsertColumn(hListWnd, 1, &lvc);

				SendMessage(hWnd, WMU_UPDATE_DATA, 0, 0);
				ListView_SetExtendedListViewStyle(hListWnd, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | 0x10000000);
				SendMessage(hWnd, WM_SIZE, 0, 0);

				SetFocus(GetDlgItem(hWnd, IDC_DLG_QUERYFILTER));
			}
			break;

			case WM_TIMER: {
				KillTimer(hWnd, IDT_EDIT_DATA);
				SendMessage(hWnd, WMU_UPDATE_DATA, 0, 0);
			}
			break;

			case WM_SIZE: {
				HWND hFilterWnd = GetDlgItem(hWnd, IDC_DLG_QUERYFILTER);
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);

				RECT rc;
				GetClientRect(hWnd, &rc);
				SetWindowPos(hFilterWnd, 0, 0, 0, rc.right - rc.left - 0, 20, SWP_NOZORDER | SWP_NOMOVE);
				SetWindowPos(hListWnd, 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top - 21, SWP_NOZORDER | SWP_NOMOVE);

				LVCOLUMN lvc;
				lvc.mask = LVCF_WIDTH;
				lvc.iSubItem = 1;
				lvc.cx = rc.right - rc.left - 130;
				ListView_SetColumn(hListWnd, 1, &lvc);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (pHdr->code == (DWORD)NM_DBLCLK)
					PostMessage(hWnd, WM_COMMAND, IDOK, 0);

				if (pHdr->code == LVN_KEYDOWN) {
					HWND hListWnd = pHdr->hwndFrom;
					NMLVKEYDOWN* kd = (LPNMLVKEYDOWN) lParam;
					int pos = ListView_GetNextItem(hListWnd, -1, LVNI_SELECTED);
					if (kd->wVKey == VK_DELETE && pos != -1) {
						int idx = GetWindowLong(hWnd, GWL_USERDATA);
						TCHAR query16[MAX_TEXT_LENGTH];
						ListView_GetItemText(hListWnd, pos, 1, query16, MAX_TEXT_LENGTH);

						char* query8 = utils::utf16to8(query16);
						prefs::deleteQuery(idx == IDM_HISTORY ? "history" : "gists", query8);
						ListView_DeleteItem(hListWnd, pos);
						ListView_SetItemState (hListWnd, pos, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						delete [] query8;
					}
				}
			}
			break;

			case WM_COMMAND: {

				if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == GetDlgItem(hWnd, IDC_DLG_QUERYFILTER) && (HWND)lParam == GetFocus()) {
					KillTimer(hWnd, IDT_EDIT_DATA);
					SetTimer(hWnd, IDT_EDIT_DATA, 300, NULL);
					return true;
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);

				if (wParam == IDOK) {
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);
					int iPos = ListView_GetNextItem(hListWnd, -1, LVNI_SELECTED);
					if (iPos == -1)
						break;

					TCHAR buf[MAX_TEXT_LENGTH];
					ListView_GetItemText(hListWnd, iPos, 1, buf, MAX_TEXT_LENGTH);

					int crPos;
					SendMessage(hEditorWnd, EM_GETSEL, (WPARAM)&crPos, (LPARAM)&crPos);
					int lineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, crPos, 0);
					int lineIdx = SendMessage(hEditorWnd, EM_LINEINDEX, lineNo, 0);
					int lineSize = SendMessage(hEditorWnd, EM_LINELENGTH, lineIdx, 0);
					if (lineSize > 0 && crPos <= lineIdx + lineSize) {
						lineIdx = SendMessage(hEditorWnd, EM_LINEINDEX, lineNo + 1, 0);
						SendMessage(hEditorWnd, EM_SETSEL, (WPARAM)lineIdx, (LPARAM)lineIdx);
					}

					SendMessage(hEditorWnd, EM_REPLACESEL, TRUE, (LPARAM)buf);
					SendMessage(hEditorWnd, EM_REPLACESEL, TRUE, (LPARAM)(buf[_tcslen(buf) - 1] != TEXT(';') ? TEXT(";\n") : TEXT("\n")));
					EndDialog(hWnd, DLG_OK);
				}
			}
			break;

			case WMU_UPDATE_DATA: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);
				HWND hFilterWnd = GetDlgItem(hWnd, IDC_DLG_QUERYFILTER);

				int size = GetWindowTextLength(hFilterWnd);
				TCHAR* filter16 = new TCHAR[size + 1]{0};
				GetWindowText(hFilterWnd, filter16, size + 1);

				int idx = GetWindowLong(hWnd, GWL_USERDATA);
				char* filter8 = utils::utf16to8(filter16);

				char * queries[prefs::get("max-query-count")];
				int count = prefs::getQueries(idx == IDM_HISTORY ? "history" : "gists", filter8, queries);

				ListView_DeleteAllItems(hListWnd);
				for (int i = 0; i < count; i++) {
					TCHAR* text16 = utils::utf8to16(queries[i]);
					TCHAR* q = _tcschr(text16, TEXT('\t'));
					if (q != NULL) {
						q += 1;
						int len = _tcslen(text16);
						int len1 = _tcslen(q);

						TCHAR time[len - len1 + 1]{0};
						_tcsncpy(time, text16, len - len1);
						LVITEM  lvi = {0};
						lvi.mask = LVIF_TEXT;
						lvi.iSubItem = 0;
						lvi.iItem = i;
						lvi.pszText = time;
						lvi.cchTextMax = len - len1 + 1;
						ListView_InsertItem(hListWnd, &lvi);

						lvi.mask = LVIF_TEXT;
						lvi.iSubItem = 1;
						lvi.iItem = i;
						lvi.pszText = q;
						lvi.cchTextMax = len1 + 1;
						ListView_SetItem(hListWnd, &lvi);
					}
					delete [] text16;
					delete queries[i];
				}

				if (count > 0)
					ListView_SetItemState (hListWnd, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);

				delete [] filter8;
				delete [] filter16;
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	char filterQuery8[MAX_TEXT_LENGTH]{0};
	BOOL CALLBACK cbDlgEditData (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				char* table8 = utils::utf16to8(editTableData16);

				char* pos = strchr(table8, '.');
				char* tablename8 = new char[255]{0};
				strncpy(tablename8, pos ? pos + 1 : table8, pos ? strlen(pos) - 1 : strlen(table8));
				SetProp(hWnd, TEXT("TABLENAME8"), (HANDLE)tablename8);

				char* schema8 = new char[255]{0};
				strncpy(schema8, pos ? table8 : "main", pos ? strlen(table8) - strlen(pos) : 4);
				SetProp(hWnd, TEXT("SCHEMA8"), (HANDLE)schema8);

				delete [] table8;

				HWND hFilterWnd = GetDlgItem(hWnd, IDC_DLG_QUERYFILTER);
				SendMessage(hWnd, WMU_UPDATE_DATA, 0 , 0);
				SetFocus(hFilterWnd);

				sprintf(filterQuery8,
					"select '\"***\" || coalesce(' || group_concat(name, ', \"\") || \"***\" || coalesce(') || ', \"\") || \"***\"', "\
					"(select type from %s.sqlite_master where tbl_name = \"%s\" and type in ('view', 'table')) type from pragma_table_info t where schema = '%s' and arg = '%s'",
					schema8, tablename8, schema8, tablename8);

				bool isTable = false;
				sqlite3_stmt *stmt;
				if ((SQLITE_OK == sqlite3_prepare_v2(db, filterQuery8, -1, &stmt, 0)) && (SQLITE_ROW == sqlite3_step(stmt))) {
					sprintf(filterQuery8, "select *, rowid from \"%s\".\"%s\" where %s like \"***%%%%%%s%%%%***\"", schema8, tablename8, sqlite3_column_text(stmt, 0));
					isTable = strcmp((char*)sqlite3_column_text(stmt, 1), "table") == 0;
				} else {
					showDbError(hWnd);
				}
				sqlite3_finalize(stmt);

				SetWindowLong(hWnd, GWL_USERDATA, +isTable);
				ShowWindow(GetDlgItem(hWnd, IDC_DLG_ROW_ADD), isTable ? SW_SHOW : SW_HIDE);

				SetWindowPos(hWnd, 0, prefs::get("x") + 40, prefs::get("y") + 80, prefs::get("width") - 80, prefs::get("height") - 120,  SWP_NOZORDER);
				ShowWindow (hWnd, prefs::get("maximized") == 1 ? SW_MAXIMIZE : SW_SHOW);

				HWND hBtnWnd = GetDlgItem(hWnd, IDC_DLG_ROW_ADD);
				SendMessage(hBtnWnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hButtonIcons[0]);
				hBtnWnd = GetDlgItem(hWnd, IDC_DLG_ROW_DEL);
				SendMessage(hBtnWnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hButtonIcons[1]);
				hBtnWnd = GetDlgItem(hWnd, IDC_DLG_REFRESH);
				SendMessage(hBtnWnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hButtonIcons[2]);
				cbOldResultList = (WNDPROC)SetWindowLong(GetDlgItem(hWnd, IDC_DLG_QUERYLIST), GWL_WNDPROC, (LONG)cbNewResultList);
			}
			break;

			case WM_TIMER: {
				KillTimer(hWnd, IDT_EDIT_DATA);
				SendMessage(hWnd, WMU_UPDATE_DATA, 0, 0);
			}
			break;

			case WM_SIZE: {
				bool isTable = GetWindowLong(hWnd, GWL_USERDATA) == 1;
				HWND hRefreshBtn = GetDlgItem(hWnd, IDC_DLG_REFRESH);
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);
				HWND hFilterWnd = GetDlgItem(hWnd, IDC_DLG_QUERYFILTER);

				RECT rc;
				GetClientRect(hWnd, &rc);
				int w = isTable ? 44 : 0;
				SetWindowPos(hRefreshBtn, 0, rc.right - rc.left - 21, 0, 20, 20, SWP_NOZORDER);
				SetWindowPos(hFilterWnd, 0, w, 0, rc.right - rc.left - 0 - w - 22, 20, SWP_NOZORDER);
				SetWindowPos(hListWnd, 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top - 21, SWP_NOZORDER | SWP_NOMOVE);
			}
			break;

			case WMU_UPDATE_DATA: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);
				HWND hFilterWnd = GetDlgItem(hWnd, IDC_DLG_QUERYFILTER);
				bool isTable = GetWindowLong(hWnd, GWL_USERDATA) == 1;

				int size = GetWindowTextLength(hFilterWnd);
				TCHAR filter16[size + 1]{0};
				GetWindowText(hFilterWnd, filter16, size + 1);

				char* filter8 = utils::utf16to8(filter16);
				char* tablename8 = (char*)GetProp(hWnd, TEXT("TABLENAME8"));
				char* schema8 = (char*)GetProp(hWnd, TEXT("SCHEMA8"));

				char query8[MAX_TEXT_LENGTH]{0};
				sprintf(query8, "select *, rowid rowid from \"%s\".\"%s\" t %s", schema8, tablename8, filter8 && strlen(filter8) ? filter8 : "");

				if (!isQueryValid(query8)) {
					sprintf(query8, "select *, rowid rowid from \"%s\".\"%s\" where %s", schema8, tablename8, filter8);

					// "where 4" or "where true" are valid filters
					bool isValid = isQueryValid(query8);
					if (!isValid || (isValid && strchr(filter8, ' ') == NULL))
						sprintf(query8, filterQuery8, filter8);
				}

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
					int colCount = sqlite3_column_count(stmt);
					if (GetProp(hWnd, TEXT("BLOBS")) == NULL) {
						bool* types = new bool[colCount]{0};
						for (int i = 0; i < colCount; i++)
							types[i] = stricmp(sqlite3_column_decltype(stmt, i), "blob") == 0;
						SetProp(hWnd, TEXT("BLOBS"), (HANDLE)types);
					}
					int rowCount = setListViewData(hListWnd, stmt);
					ListView_SetColumnWidth(hListWnd, colCount, 0); // last column is rowid

					TCHAR buf[256]{0};
					_stprintf(buf, TEXT("%s \"%s\" [%s%i rows]"), isTable ? TEXT("Table") : TEXT("View"), editTableData16, rowCount < 0 ? TEXT("Show only first ") : TEXT(""), abs(rowCount));
					SetWindowText(hWnd, buf);
				} else {
					showDbError(hWnd);
					sqlite3_finalize(stmt);
				}

				ListView_SetItemState(hListWnd, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

				delete [] filter8;
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				bool isTable = GetWindowLong(hWnd, GWL_USERDATA) == 1;
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);

				if (pHdr->code == LVN_COLUMNCLICK) {
					NMLISTVIEW* pLV = (NMLISTVIEW*)lParam;
					return sortListView(pHdr->hwndFrom, pLV->iSubItem);
				}

				if (pHdr->code == (DWORD)NM_RCLICK && pHdr->hwndFrom == hListWnd) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					currCell = {hListWnd, ia->iItem, ia->iSubItem};
					POINT p;
					GetCursorPos(&p);

					TCHAR buf[10];
					ListView_GetItemText(hListWnd, ia->iItem, ia->iSubItem, buf, 10);

					bool* blobs = (bool*)GetProp(hWnd, TEXT("BLOBS"));
					HMENU hMenu = !isTable || ListView_GetSelectedCount(hListWnd) != 1 ?
						hResultMenu :
						(blobs[ia->iSubItem - 1] || !_tcscmp(buf, TEXT("(BLOB)"))) ? hBlobMenu : hEditDataMenu;
					TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hWnd, NULL);
				}

				if (pHdr->code == (DWORD)NM_CLICK && GetAsyncKeyState(VK_MENU)) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					return showRefData(hListWnd, ia->iItem, ia->iSubItem);
				}

				if (pHdr->code == (DWORD)NM_DBLCLK && pHdr->hwndFrom == hListWnd && !isTable) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					if (ia->iItem != -1)
						SendMessage(hWnd, WM_COMMAND, IDM_ROW_EDIT, 0);
				}

				if (pHdr->code == LVN_KEYDOWN && pHdr->hwndFrom == hListWnd) {
					NMLVKEYDOWN* kd = (LPNMLVKEYDOWN) lParam;
					if (kd->wVKey == 0x43 && GetKeyState(VK_CONTROL)) { // Ctrl + C
						currCell = {hListWnd, 0, 0};
						PostMessage(hWnd, WM_COMMAND, IDM_RESULT_COPY_ROW, 0);
					}

					if (kd->wVKey == 0x41 && GetKeyState(VK_CONTROL)) // Ctrl + A
						ListView_SetItemState(hListWnd, -1, LVIS_SELECTED, LVIS_SELECTED);

					if (isTable && kd->wVKey == VK_DELETE)
						PostMessage(hWnd, WM_COMMAND, IDM_ROW_DELETE, 0);
					bool isNum = kd->wVKey >= 0x31 && kd->wVKey <= 0x39;
					bool isNumPad = kd->wVKey >= 0x61 && kd->wVKey <= 0x69;
					if ((isNum || isNumPad) && GetKeyState(VK_CONTROL)) // Ctrl + 1-9
						return sortListView(pHdr->hwndFrom, kd->wVKey - (isNum ? 0x31 : 0x61) + 1 );
				}

				if (isTable && pHdr->code == (DWORD)NM_DBLCLK && pHdr->hwndFrom == hListWnd) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;

					if (ia->iItem == -1)
						return SendMessage(hWnd, WM_COMMAND, IDC_DLG_ROW_ADD, 0);

					if (ia->iSubItem == 0 || ia->iSubItem == Header_GetItemCount(ListView_GetHeader(hListWnd)) - 1)
						return true;

					RECT rect;
					ListView_GetSubItemRect(hListWnd, ia->iItem, ia->iSubItem, LVIR_BOUNDS, &rect);
					int h = rect.bottom - rect.top;
					int w = ListView_GetColumnWidth(hListWnd, ia->iSubItem);

					TCHAR buf[MAX_TEXT_LENGTH];
					ListView_GetItemText(hListWnd, ia->iItem, ia->iSubItem, buf, MAX_TEXT_LENGTH);

					if (_tcscmp(buf, TEXT("(BLOB)")) == 0)
						return 1;

					bool isRichEdit = GetAsyncKeyState(VK_CONTROL) || _tcslen(buf) > 100 || _tcschr(buf, TEXT('\n'));
					HWND hEdit = isRichEdit ?
						CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("RICHEDIT50W"), buf, WS_CHILD | WS_VISIBLE | ES_WANTRETURN | ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL, rect.left, rect.top - 2, 300, 150, hListWnd, 0, GetModuleHandle(NULL), NULL):
						CreateWindowEx(0, WC_EDIT, buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rect.left, rect.top, w, h, hListWnd, 0, GetModuleHandle(NULL), NULL);

					SetWindowLong(hEdit, GWL_USERDATA, MAKELPARAM(ia->iItem, ia->iSubItem));
					int end = GetWindowTextLength(hEdit);
					SendMessage(hEdit, EM_SETSEL, end, end);
					if (isRichEdit) {
						setEditorFont(hEdit);
					}
					else
						SendMessage(hEdit, WM_SETFONT, (LPARAM)hDefFont, true);
					SetFocus(hEdit);

					cbOldEditDataEdit = (WNDPROC)SetWindowLong(hEdit, GWL_WNDPROC, (LONG)cbNewEditDataEdit);
				}
			}
			break;

			case WM_COMMAND: {
				WORD cmd = LOWORD(wParam);
				bool isTable = GetWindowLong(hWnd, GWL_USERDATA) == 1;
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);

				if (wParam == IDOK) { // User push Enter
					int pos = ListView_GetNextItem(hListWnd, -1, LVNI_SELECTED);
					if (hListWnd == GetFocus() && pos != -1) {
						currCell = {hListWnd, pos, 0};
						PostMessage(hWnd, WM_COMMAND, IDM_ROW_EDIT, 0);
					}
				}

				if (cmd == IDC_DLG_CANCEL || cmd == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);

				if (cmd == IDM_RESULT_COPY_CELL || cmd == IDM_RESULT_COPY_ROW || cmd == IDM_RESULT_EXPORT)
					onListViewMenu(cmd, true);

				if (cmd == IDC_DLG_ROW_ADD)
					DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_ROW), hWnd, (DLGPROC)&cbDlgRow, MAKELPARAM(ROW_ADD, 0));

				if (cmd == IDC_DLG_ROW_DEL)
					SendMessage(hWnd, WM_COMMAND, MAKELPARAM(IDM_ROW_DELETE, 0), 0);

				if (cmd == IDC_DLG_REFRESH)
					SendMessage(hWnd, WMU_UPDATE_DATA, 0, 0);

				if (cmd == IDM_ROW_EDIT)
					DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_ROW), hWnd, (DLGPROC)&cbDlgRow, MAKELPARAM(isTable ? ROW_EDIT: ROW_VIEW, 0));

				if (cmd == IDM_ROW_DELETE) {
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_QUERYLIST);
					int count = ListView_GetSelectedCount(hListWnd);
					if (!count)
						return true;

					char* placeholders8 = new char[count * 2]{0}; // count = 3 => ?, ?, ?
					for (int i = 0; i < count * 2 - 1; i++)
						placeholders8[i] = i % 2 ? ',' : '?';
					placeholders8[count * 2 - 1] = '\0';

					char* sql8 = new char[128 + count * 2]{0};
					char* tablename8 = (char*)GetProp(hWnd, TEXT("TABLENAME8"));
					char* schema8 = (char*)GetProp(hWnd, TEXT("SCHEMA8"));

					sprintf(sql8, "delete from \"%s\".\"%s\" where rowid in (%s)", schema8, tablename8, placeholders8);
					delete [] placeholders8;

					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db, sql8, -1, &stmt, 0)) {
						int colCount = Header_GetItemCount(ListView_GetHeader(hListWnd));
						int pos = -1;
						TCHAR buf16[64];
						for (int i = 0; i < count; i++) {
							pos = ListView_GetNextItem(hListWnd, pos, LVNI_SELECTED);
							ListView_GetItemText(hListWnd, pos, colCount - 1, buf16, 128);
							sqlite3_bind_int64(stmt, i + 1, _tcstol(buf16, NULL, 10));
						}

						if (SQLITE_DONE == sqlite3_step(stmt)) {
							pos = -1;
							while((pos = ListView_GetNextItem(hListWnd, -1, LVNI_SELECTED)) != -1)
								ListView_DeleteItem(hListWnd, pos);
							ListView_SetItemState (hListWnd, pos, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						} else {
							showDbError(hWnd);
						}

						sqlite3_finalize(stmt);
					}
					delete [] sql8;
				}

				if (cmd == IDM_BLOB_NULL || cmd == IDM_BLOB_IMPORT || cmd == IDM_BLOB_EXPORT) {
					TCHAR path16[MAX_PATH]{0};
					TCHAR filter16[] = TEXT("Images (*.jpg, *.gif, *.png, *.bmp)\0*.jpg;*.jpeg;*.gif;*.png;*.bmp\0Binary(*.bin,*.dat)\0*.bin,*.dat\0All\0*.*\0");
					bool isOK = (cmd == IDM_BLOB_IMPORT && utils::openFile(path16, filter16)) ||
						(cmd == IDM_BLOB_EXPORT && utils::saveFile(path16, filter16)) ||
						(cmd == IDM_BLOB_NULL && MessageBox(hWnd, TEXT("Are you sure to reset the cell?"), TEXT("Erase confirmation"), MB_OKCANCEL) == IDOK);

					if (!isOK)
						return 1;

					int rowNo = currCell.iItem;
					int colNo = currCell.iSubItem;
					HWND hListWnd = currCell.hListWnd;
					HWND hHeader = (HWND)ListView_GetHeader(hListWnd);

					TCHAR column16[64];
					HDITEM hdi = {0};
					hdi.mask = HDI_TEXT;
					hdi.pszText = column16;
					hdi.cchTextMax = 64;

					if (!hHeader || !Header_GetItem(hHeader, colNo, &hdi))
						return 1;

					TCHAR rowid16[32] = {0};
					int colCount = Header_GetItemCount(hHeader);
					ListView_GetItemText(hListWnd, rowNo, colCount - 1, rowid16, 64);
					long rowid = _tcstol(rowid16, NULL, 10);

					char* tablename8 = (char*)GetProp(hWnd, TEXT("TABLENAME8"));
					char* schema8 = (char*)GetProp(hWnd, TEXT("SCHEMA8"));
					char* column8 = utils::utf16to8(column16);

					char query8[256] = {0};
					if (cmd == IDM_BLOB_EXPORT) {
						sprintf(query8, "select %s from \"%s\".\"%s\" where rowid = ?1", column8, schema8, tablename8);
					} else {
						sprintf(query8, "update \"%s\".\"%s\" set \"%s\" = ?1 where rowid = ?2", schema8, tablename8, column8);
					}

					char* path8 = utils::utf16to8(path16);
					sqlite3_stmt *stmt;

					int	rc = SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0);
					if (rc && (cmd == IDM_BLOB_NULL)) {
						sqlite3_bind_null(stmt, 1);
						sqlite3_bind_int64(stmt, 2, rowid);
						rc = SQLITE_DONE == sqlite3_step(stmt);
						ListView_SetItemText(hListWnd, rowNo, colNo, TEXT(""));
					}

					if (rc && (cmd == IDM_BLOB_IMPORT)) {
						FILE *fp = fopen (path8 , "rb");
						if (!fp)
							MessageBox(hWnd, TEXT("Opening the file for reading failed."), TEXT("Info"), MB_OK);

						if (rc && fp) {
							fseek(fp, 0L, SEEK_END);
							long size = ftell(fp);
							rewind(fp);

							char* data8 = new char[size]{0};
							fread(data8, size, 1, fp);
							fclose(fp);

							sqlite3_bind_blob(stmt, 1, data8, size, SQLITE_TRANSIENT);
							sqlite3_bind_int64(stmt, 2, rowid);
							rc = SQLITE_DONE == sqlite3_step(stmt);
							delete [] data8;

							ListView_SetItemText(hListWnd, rowNo, colNo, TEXT("(BLOB)"));
						}
					}

					if (rc && (cmd == IDM_BLOB_EXPORT)) {
						sqlite3_bind_int64(stmt, 1, rowid);
						rc = SQLITE_ROW == sqlite3_step(stmt);
						FILE *fp = fopen (path8 , "wb");
						if (!fp)
							MessageBox(hWnd, TEXT("Opening the file for writing failed."), TEXT("Info"), MB_OK);
						if (rc && fp) {
							fwrite(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0), 1, fp);
							fclose(fp);
						}
					}

					sqlite3_finalize(stmt);
					if (!rc)
						showDbError(hWnd);

					delete [] column8;
					delete [] path8;
				}

				if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == GetDlgItem(hWnd, IDC_DLG_QUERYFILTER) && (HWND)lParam == GetFocus()) {
					KillTimer(hWnd, IDT_EDIT_DATA);
					SetTimer(hWnd, IDT_EDIT_DATA, 300, NULL);
				}
			}
			break;

			case WM_CLOSE: {
				char* tablename8 = (char*)GetProp(hWnd, TEXT("TABLENAME8"));
				delete [] tablename8;
				RemoveProp(hWnd, TEXT("TABLENAME8"));

				char* schema8 = (char*)GetProp(hWnd, TEXT("SCHEMA8"));
				delete [] schema8;
				RemoveProp(hWnd, TEXT("SCHEMA8"));

				bool* blobs = (bool*)GetProp(hWnd, TEXT("BLOBS"));
				delete [] blobs;
				RemoveProp(hWnd, TEXT("BLOBS"));

				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgRow (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				int mode = LOWORD(lParam);
				int isResultWnd = HIWORD(lParam);
				HWND hListWnd = isResultWnd ? (HWND)GetWindowLong(hTabWnd, GWL_USERDATA) : GetDlgItem(GetWindow(hWnd, GW_OWNER), IDC_DLG_QUERYLIST);
				HWND hHeader = ListView_GetHeader(hListWnd);
				int colCount = Header_GetItemCount(hHeader) - !isResultWnd;

				if (!hHeader || !colCount)
					EndDialog(hWnd, DLG_CANCEL);

				for (int colNo = 1; colNo < colCount; colNo++) {
					TCHAR colName[255];
					HDITEM hdi = { 0 };
					hdi.mask = HDI_TEXT;
					hdi.pszText = colName;
					hdi.cchTextMax = 255;
					Header_GetItem(hHeader, colNo, &hdi);

					CreateWindow(WC_STATIC, colName, WS_VISIBLE | WS_CHILD | SS_RIGHT, 5, 5 + 20 * (colNo - 1), 70, 18, hWnd, (HMENU)(IDC_ROW_LABEL +  colNo), GetModuleHandle(0), 0);
					CreateWindow(WC_EDIT, NULL, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL | (mode == ROW_VIEW ? ES_READONLY : 0), 80, 3 + 20 * (colNo - 1), 285, 18, hWnd, (HMENU)(IDC_ROW_EDIT + colNo), GetModuleHandle(0), 0);
					CreateWindow(WC_BUTTON, TEXT(">"), WS_VISIBLE | WS_CHILD | BS_FLAT, 370, 3 + 20 * (colNo - 1), 18, 18, hWnd, (HMENU)(IDC_ROW_SWITCH + colNo), GetModuleHandle(0), 0);
				}
				EnumChildWindows(hWnd, (WNDENUMPROC)cbEnumChildren, (LPARAM)ACTION_SETDEFFONT);
				SetWindowPos(hWnd, 0, 0, 0, 400, colCount * 20 + 140, SWP_NOMOVE | SWP_NOZORDER);

				SetWindowText(hWnd, mode == ROW_ADD ? TEXT("New row") : mode == ROW_EDIT ? TEXT("Edit row") : TEXT("View row"));

				HWND hOkBtn = GetDlgItem(hWnd, IDC_DLG_OK);
				HWND hCancelBtn = GetDlgItem(hWnd, IDC_DLG_CANCEL);
				SetWindowText(hOkBtn, mode == ROW_ADD ? TEXT("Save and New") : mode == ROW_EDIT ? TEXT("Save and Next") : TEXT("Next"));
				SetWindowPos(hOkBtn, 0, 202, colCount * 20 + 86, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
				SetWindowPos(hCancelBtn, 0, 297, colCount * 20 + 86, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

				SetWindowLong(hWnd, GWL_USERDATA, MAKELPARAM(mode, colCount));
				SetWindowLong(GetDlgItem(hWnd, IDC_DLG_USERDATA), GWL_USERDATA, (LONG)hListWnd);

				if (mode != ROW_ADD)
					SendMessage(hWnd, WMU_SET_DLG_ROW_DATA, 0, 0);
				SetFocus(GetDlgItem(hWnd, IDC_ROW_EDIT + 1));
			}
			break;

			case WMU_SET_DLG_ROW_DATA: {
				HWND hListWnd = (HWND)GetWindowLong(GetDlgItem(hWnd, IDC_DLG_USERDATA), GWL_USERDATA);
				int colCount = HIWORD(GetWindowLong(hWnd, GWL_USERDATA));

				TCHAR val[MAX_TEXT_LENGTH];
				for (int i = 0; i < colCount; i++) {
					ListView_GetItemText(hListWnd, currCell.iItem, i, val, MAX_TEXT_LENGTH);
					HWND hEdit = GetDlgItem(hWnd, IDC_ROW_EDIT + i);
					SetWindowText(hEdit, val);
					bool isBlob = _tcscmp(val, TEXT("(BLOB)")) == 0;
					EnableWindow(hEdit, !isBlob);
					EnableWindow(GetDlgItem(hWnd, IDC_ROW_SWITCH + i), !isBlob);
				}
				return true;
			}

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_COMMAND: {
				if (wParam >= IDC_ROW_SWITCH && wParam < IDC_ROW_SWITCH + 100) {
					int no = wParam - IDC_ROW_SWITCH;
					HWND hEdit = GetDlgItem(hWnd, IDC_ROW_EDIT + no);
					int size = GetWindowTextLength(hEdit);
					TCHAR* text = new TCHAR[size + 1]{0};
					GetWindowText(hEdit, text, size + 1);

					RECT rect;
					GetWindowRect(hEdit, &rect);

					POINT p = {rect.left, rect.top};
					ScreenToClient(hWnd, &p);

					TCHAR cls[255];
					GetClassName(hEdit, cls, 255);

					int readable =  GetWindowLong(hEdit, GWL_STYLE) & ES_READONLY ? ES_READONLY : ES_AUTOHSCROLL;
					DestroyWindow(hEdit);

					bool isEdit = !_tcscmp(WC_EDIT, cls);
					hEdit = CreateWindow(
						isEdit ? TEXT("RICHEDIT50W") : WC_EDIT,
						text,
						isEdit ?
							WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | readable:
							WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL | readable,
						p.x, p.y, rect.right - rect.left, isEdit ? 100 : 18, hWnd, (HMENU)(IDC_ROW_EDIT + no), GetModuleHandle(0), 0);
					if (isEdit)
						SendMessage(hEdit, EM_SETWORDWRAPMODE, WBF_WORDWRAP, 0);
					SetWindowPos(hEdit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					SendMessage(hEdit, WM_SETFONT, (LPARAM)hDefFont, true);
					SetFocus(hEdit);
					SetWindowText(GetDlgItem(hWnd, IDC_ROW_SWITCH + no), isEdit ? TEXT("V") : TEXT(">"));

					delete [] text;
				}

				if (wParam == IDOK) {
					int id = GetDlgCtrlID(GetFocus());
					if (id >= IDC_ROW_EDIT && wParam < IDC_ROW_EDIT + 100)
						return GetAsyncKeyState(VK_CONTROL) ? SendMessage(hWnd, WM_COMMAND, IDC_DLG_OK, 0) : SendMessage(hWnd, WM_NEXTDLGCTL, 0, 0);
				}

				if (wParam == IDC_DLG_OK) {
					HWND hListWnd = (HWND)GetWindowLong(GetDlgItem(hWnd, IDC_DLG_USERDATA), GWL_USERDATA);
					int mode = LOWORD(GetWindowLong(hWnd, GWL_USERDATA));
					int colCount = HIWORD(GetWindowLong(hWnd, GWL_USERDATA));

					auto changeCurrentItem = [hListWnd, mode]() {
						ListView_SetItemState( hListWnd, -1, LVIF_STATE, LVIS_SELECTED);

						int colCount = ListView_GetItemCount(hListWnd);
						currCell.iItem = mode != ROW_ADD ? (currCell.iItem + 1) % colCount : colCount - 1;
						ListView_SetItemState (hListWnd, currCell.iItem, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						if (!ListView_EnsureVisible(hListWnd, currCell.iItem, false)) {
							RECT rect = {0};
							ListView_GetItemRect(hListWnd, currCell.iItem, &rect, LVIR_LABEL);
							ListView_Scroll(hListWnd, 0, rect.bottom);
						}
					};

					if (mode == ROW_VIEW) {
						changeCurrentItem();
						SendMessage(hWnd, WMU_SET_DLG_ROW_DATA, 0, 0);
						return true;
					}

					TCHAR* columns16[colCount];
					char* values8[colCount];
					TCHAR* values16[colCount];
					char* columns8[colCount];

					char* table8 = utils::utf16to8(editTableData16);
					char* pos = strchr(table8, '.');

					char* tablename8 = new char[255]{0};
					strncpy(tablename8, pos ? pos + 1 : table8, pos ? strlen(pos) - 1 : strlen(table8));

					char* schema8 = new char[255]{0};
					strncpy(schema8, pos ? table8 : "main", pos ? strlen(table8) - strlen(pos) : 4);

					delete [] table8;

					int len = 0;
					// A first column in the listview is always a rowno. Should be ignored.
					for (int i = 1; i < colCount; i++) {
						if (!IsWindowEnabled(GetDlgItem(hWnd, IDC_ROW_EDIT + i)))
							continue;

						HWND hLabel = GetDlgItem(hWnd, IDC_ROW_LABEL + i);
						len = GetWindowTextLength(hLabel);
						columns16[i] = new TCHAR[len + 1]{0};
						GetWindowText(hLabel, columns16[i], len + 1);
						columns8[i] = utils::utf16to8(columns16[i]);

						HWND hEdit = GetDlgItem(hWnd, IDC_ROW_EDIT + i);
						len = GetWindowTextLength(hEdit);
						values16[i] = new TCHAR[len + 1]{0};
						GetWindowText(hEdit, values16[i], len + 1);
						values8[i] = utils::utf16to8(values16[i]);
					}

					char* sql8 = new char[MAX_TEXT_LENGTH]{0};
					char buf8[256];
					sprintf(buf8, mode == ROW_ADD ? "insert into \"%s\".\"%s\" (" : "update \"%s\".\"%s\" set ", schema8, tablename8);
					strcat(sql8, buf8);
					int valCount = 0;
					for (int i = 1; i < colCount; i++) {
						if (!IsWindowEnabled(GetDlgItem(hWnd, IDC_ROW_EDIT + i)))
							continue;

						sprintf(buf8, mode == ROW_ADD ? "%s\"%s\"" : "%s\"%s\" = ?", valCount > 0 ? ", " : "", columns8[i]);
						strcat(sql8, buf8);

						valCount++;
					}

					if (mode == ROW_ADD) {
						char* placeholders8 = new char[(valCount + 1) * 2]{0}; // count = 3 => ?, ?, ?
						for (int i = 0; i < (valCount + 1) * 2 - 3; i++)
							placeholders8[i] = i % 2 ? ',' : '?';
						placeholders8[(valCount + 1) * 2 - 1] = '\0';
						strcat(sql8, ") values (");
						strcat(sql8, placeholders8);
						strcat(sql8, ")");
						delete [] placeholders8;
					} else {
						strcat(sql8, " where rowid = ?");
					}

					struct HookUserData {
						char *table;
						int op;
						sqlite3_int64 rowid;
					};
					HookUserData hud = {tablename8, mode == ROW_ADD ? SQLITE_INSERT : SQLITE_UPDATE, -1};

					auto cbHook = [](void *user_data, int op, char const *dbName, char const *table, sqlite3_int64 rowid) {
						HookUserData* hud = (HookUserData*)user_data;
						if (!stricmp(hud->table, table) && hud->op == op)
							hud->rowid = rowid;
					};
					sqlite3_update_hook(db, cbHook, &hud);

					sqlite3_stmt *stmt;
					bool rc = SQLITE_OK == sqlite3_prepare_v2(db, sql8, -1, &stmt, 0);
					if (rc) {
						int valNo = 1;
						for (int i = 1; i < colCount; i++)
							if (IsWindowEnabled(GetDlgItem(hWnd, IDC_ROW_EDIT + i))) {
								utils::sqlite3_bind_variant(stmt, valNo, values8[i]);
								valNo++;
							}

						if (mode == ROW_EDIT) {
							TCHAR rowid[64];
							ListView_GetItemText(hListWnd, currCell.iItem, colCount, rowid, 64);
							sqlite3_bind_int64(stmt, valNo, _tcstol(rowid, NULL, 10));
						}

						rc = SQLITE_DONE == sqlite3_step(stmt);
						sqlite3_finalize(stmt);
						sqlite3_update_hook(db, NULL, NULL);
					}

					if (rc) {
						char sql8[255];
						sprintf(sql8, "select *, rowid from \"%s\".\"%s\" where rowid = ?", schema8, tablename8);

						sqlite3_stmt *stmt;
						sqlite3_prepare_v2(db, sql8, -1, &stmt, 0);
						sqlite3_bind_int64(stmt, 1, hud.rowid);

						if (SQLITE_ROW == sqlite3_step(stmt)) {
							int iItem = mode == ROW_ADD ? ListView_GetItemCount(hListWnd) : currCell.iItem;
							for (int i = 0; i < sqlite3_column_count(stmt); i++) {
								int colType = sqlite3_column_type(stmt, i);
								TCHAR* value16 = utils::utf8to16(
									colType == SQLITE_NULL ? "" :
									colType == SQLITE_BLOB ? "(BLOB)" :
									(char *) sqlite3_column_text(stmt, i));

								LVITEM  lvi = {0};
								if (mode == ROW_ADD && i == 0) {
									lvi.mask = 0;
									lvi.iSubItem = 0;
									lvi.iItem = iItem;
									ListView_InsertItem(hListWnd, &lvi);
								}

								lvi.mask = LVIF_TEXT;
								lvi.iSubItem = i + 1;
								lvi.iItem = iItem;
								lvi.pszText = value16;
								lvi.cchTextMax = _tcslen(value16) + 1;

								ListView_SetItem(hListWnd, &lvi);
								delete [] value16;
							}
						}
						sqlite3_finalize(stmt);

						changeCurrentItem();
						if (mode == ROW_EDIT)
							SendMessage(hWnd, WMU_SET_DLG_ROW_DATA, 0, 0);
						SetFocus(GetDlgItem(hWnd, IDC_ROW_EDIT + 1));
					} else
						showDbError(hWnd);

					for (int i = 1; i < colCount; i++) {
						delete [] columns16[i];
						delete [] columns8[i];
						delete [] values16[i];
						delete [] values8[i];
					}

					delete [] tablename8;
					delete [] schema8;
					delete [] sql8;
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);

			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgAddColumn (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				TCHAR buf[256];
				_stprintf(buf, TEXT("Add column to \"%s\""), editTableData16);
				SetWindowText(hWnd, buf);

				HWND hColType = GetDlgItem(hWnd, IDC_DLG_COLTYPE);
				for (int i = 0; DATATYPES16[i]; i++)
					ComboBox_AddString(hColType, DATATYPES16[i]);
				ComboBox_SetCurSel(hColType, 0);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK) {
					TCHAR colName16[255] = {0};
					GetDlgItemText(hWnd, IDC_DLG_COLNAME, colName16, 255);

					TCHAR colType16[64] = {0};
					GetDlgItemText(hWnd, IDC_DLG_COLTYPE, colType16, 64);

					TCHAR _check16[255] = {0}, check16[300] = {0};
					GetDlgItemText(hWnd, IDC_DLG_CHECK, _check16, 255);
					_stprintf(check16, _tcslen(_check16) > 0 ? TEXT("check(%s)") : TEXT("%s"), _check16);

					TCHAR _defValue16[255] = {0}, defValue16[300] = {0};
					GetDlgItemText(hWnd, IDC_DLG_DEFVALUE, _defValue16, 255);
					_stprintf(defValue16, _tcslen(_defValue16) > 0 ? TEXT("default \"%s\"") : TEXT("%s"), _defValue16);

					bool isNotNull = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISNOTNULL));
					bool isUnique = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISUNIQUE));
					TCHAR query16[2000] = {0};
					_stprintf(query16, TEXT("alter table \"%s\" add column \"%s\" %s %s %s %s %s"),
						editTableData16, colName16, colType16, isNotNull ? TEXT("NOT NULL") : TEXT(""), defValue16, check16, isUnique ? TEXT("UNIQUE") : TEXT(""));

					char* query8 = utils::utf16to8(query16);
					if (SQLITE_OK != sqlite3_exec(db, query8, NULL, NULL, NULL))
						showDbError(hWnd);
					else
						EndDialog(hWnd, 0);
					delete [] query8;
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_CLOSE:
				EndDialog(hWnd, DLG_CANCEL);
				break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgFind (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hEditorWnd = (HWND)lParam;
				int start, end;
				SendMessage(hEditorWnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
				TCHAR* word;
				if (start != end) {
					word = new TCHAR[end - start + 1]{0};
					TEXTRANGE tr{{start, end}, word};
					SendMessage(hEditorWnd, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				} else {
					word = getWordFromCursor(hEditorWnd);
				}
				SetDlgItemText(hWnd, IDC_DLG_FIND, word);
				delete [] word;
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK || wParam == IDOK) {
					GetDlgItemText(hWnd, IDC_DLG_FIND, searchString, 255);
					EndDialog(hWnd, DLG_OK);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_CLOSE:
				EndDialog(hWnd, DLG_CANCEL);
				break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgDDL (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hEditorWnd = GetDlgItem(hWnd, IDC_DLG_EDITOR);
				setEditorFont(hEditorWnd);
				SendMessage(hEditorWnd, WM_SETTEXT, (WPARAM)0, (LPARAM)lParam);

				processHightlight(hEditorWnd, true, false);
			}
			break;

			case WM_SIZE: {
				HWND hEditorWnd = GetDlgItem(hWnd, IDC_DLG_EDITOR);
				RECT rc = {0};
				GetClientRect(hWnd, &rc);
				SetWindowPos(hEditorWnd, 0, 0, 0, rc.right, rc.bottom, SWP_NOMOVE | SWP_NOZORDER);
				SendMessage(hEditorWnd, EM_SETSEL, (WPARAM)0, (LPARAM)0);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_CLOSE:
				EndDialog(hWnd, DLG_CANCEL);
				break;
		}

		return false;
	}


	BOOL CALLBACK cbEnumFont(LPLOGFONT lplf, LPNEWTEXTMETRIC lpntm, DWORD fontType, LPVOID hWnd)  {
		if (fontType & TRUETYPE_FONTTYPE && lplf->lfFaceName[0] != TEXT('@'))
			ComboBox_AddString((HWND)hWnd, lplf->lfFaceName);
		return true;
	}

	BOOL CALLBACK cbDlgSettings (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hFontSize = GetDlgItem(hWnd, IDC_DLG_FONT_SIZE);
				int fontSize = prefs::get("font-size");
				int sizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24};
				int idx = 0;
				for (int i = 0; i < 11; i++) {
					idx = fontSize == sizes[i] ? i : idx;
					TCHAR buf[3];
					_stprintf(buf, TEXT("%i"), sizes[i]);
					ComboBox_AddString(hFontSize, buf);
				}
				ComboBox_SetCurSel(hFontSize, idx);

				HWND hFontFamily = GetDlgItem(hWnd, IDC_DLG_FONT_FAMILY);
				HDC hDC = GetDC(hMainWnd);
				LOGFONT lf = {0};
				lf.lfFaceName[0] = TEXT('\0');
				lf.lfCharSet = GetTextCharset(hDC);
				EnumFontFamiliesEx(hDC, &lf, (FONTENUMPROC) cbEnumFont, (LPARAM)hFontFamily, 0);
				ReleaseDC(hMainWnd, hDC);
				char* fontFamily8 = prefs::get("font-family", "Courier New");
				TCHAR* fontFamily16 = utils::utf8to16(fontFamily8);
				ComboBox_SetCurSel(hFontFamily, ComboBox_FindString(hFontFamily, -1, fontFamily16));
				delete [] fontFamily16;
				delete [] fontFamily8;

				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_AUTOLOAD), prefs::get("autoload-extensions") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_RESTORE_DB), prefs::get("restore-db") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_RESTORE_EDITOR), prefs::get("restore-editor") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_USE_HIGHLIGHT), prefs::get("use-highlight") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_USE_LEGACY), prefs::get("use-legacy-rename") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_EXIT_BY_ESCAPE), prefs::get("exit-by-escape") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_QUERY_IN_CURR_TAB), prefs::get("query-data-in-current-tab") ? BST_CHECKED : BST_UNCHECKED);

				TCHAR buf[255];
				_stprintf(buf, TEXT("%i"), prefs::get("row-limit"));
				SetDlgItemText(hWnd, IDC_DLG_ROW_LIMIT, buf);

				char* startup8 = prefs::get("startup", "");
				TCHAR* startup16 = utils::utf8to16(startup8);
				SetDlgItemText(hWnd, IDC_DLG_STARTUP, startup16);
				delete [] startup16;
				delete [] startup8;

				HWND hIndent = GetDlgItem(hWnd, IDC_DLG_INDENT);
				for (int i = 0; i < 3; i++)
					ComboBox_AddString(hIndent, INDENT_LABELS[i]);
				ComboBox_SetCurSel(hIndent, prefs::get("editor-indent"));
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK) {
					TCHAR buf[255];
					GetDlgItemText(hWnd, IDC_DLG_FONT_FAMILY, buf, 255);
					char* fontFamily8 = utils::utf16to8(buf);
					prefs::set("font-family", fontFamily8);
					delete [] fontFamily8;
					GetDlgItemText(hWnd, IDC_DLG_FONT_SIZE, buf, 255);
					prefs::set("font-size", _tcstol(buf, NULL, 10));
					prefs::set("autoload-extensions", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_AUTOLOAD)));
					prefs::set("restore-db", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_RESTORE_DB)));
					prefs::set("restore-editor", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_RESTORE_EDITOR)));
					prefs::set("use-highlight", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_USE_HIGHLIGHT)));
					prefs::set("use-legacy-rename", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_USE_LEGACY)));
					prefs::set("exit-by-escape", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_EXIT_BY_ESCAPE)));
					prefs::set("query-data-in-current-tab", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_QUERY_IN_CURR_TAB)));
					prefs::set("editor-indent", ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_INDENT)));

					GetDlgItemText(hWnd, IDC_DLG_ROW_LIMIT, buf, 255);
					prefs::set("row-limit", (int)_tcstod(buf, NULL));

					setEditorFont(hEditorWnd);
					setTreeFont(hTreeWnd);
					sqlite3_exec(db, prefs::get("use-legacy-rename") ? "pragma legacy_alter_table = 1" : "pragma legacy_alter_table = 0", 0, 0, 0);

					TCHAR startup16[MAX_TEXT_LENGTH]{0};
					GetDlgItemText(hWnd, IDC_DLG_STARTUP, startup16, MAX_TEXT_LENGTH - 1);
					char* startup8 = utils::utf16to8(startup16);
					prefs::set("startup", startup8);
					delete [] startup8;

					if (!prefs::get("use-highlight")) {
						CHARFORMAT cf = {0};
						cf.cbSize = sizeof(CHARFORMAT2) ;
						SendMessage(hEditorWnd, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf);
						cf.dwMask = CFM_COLOR;
						cf.dwEffects = 0;
						cf.crTextColor = RGB(0, 0, 0);

						SendMessage(hEditorWnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) &cf);
					}

					EndDialog(hWnd, DLG_OK);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	LRESULT CALLBACK cbNewEditDataEdit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_GETDLGCODE)
			return (DLGC_WANTALLKEYS | CallWindowProc(cbOldEditDataEdit, hWnd, msg, wParam, lParam));

		switch(msg){
			case WM_KILLFOCUS: {
				SendMessage(hWnd, WMU_SAVE_DATA, 0, 0);
				DestroyWindow(hWnd);
			}
			break;

			case WM_KEYDOWN: {
				if (wParam == VK_RETURN) {
					int style = GetWindowLong(hWnd, GWL_STYLE);
					if((style & ES_MULTILINE) == ES_MULTILINE && GetAsyncKeyState(VK_CONTROL))
						break;

					SendMessage(hWnd, WMU_SAVE_DATA, 0, 0);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}

				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WMU_SAVE_DATA: {
				HWND hListWnd = GetParent(hWnd);
				int size = GetWindowTextLength(hWnd);
				TCHAR* value16 = new TCHAR[size + 1]{0};
				GetWindowText(hWnd, value16, size + 1);

				TCHAR column16[64];
				HDITEM hdi = {0};
				hdi.mask = HDI_TEXT;
				hdi.pszText = column16;
				hdi.cchTextMax = 64;

				HWND hHeader = (HWND)ListView_GetHeader(hListWnd);
				int data = GetWindowLong(hWnd, GWL_USERDATA);
				int rowNo = LOWORD(data);
				int colNo = HIWORD(data);

				if (hHeader != NULL && Header_GetItem(hHeader, colNo, &hdi)) {
					TCHAR query16[256];
					_stprintf(query16, TEXT("update \"%s\" set %s = ?1 where rowid = ?2"), editTableData16, column16);

					int colCount = Header_GetItemCount(hHeader);
					ListView_GetItemText(hListWnd, rowNo, colCount - 1, column16, 64);
					long rowid = _tcstol(column16, NULL, 10);

					char* query8 = utils::utf16to8(query16);
					char* value8 = utils::utf16to8(value16);

					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
						utils::sqlite3_bind_variant(stmt, 1, value8);
						sqlite3_bind_int64(stmt, 2, rowid);
						if (SQLITE_DONE == sqlite3_step(stmt))
							ListView_SetItemText(hListWnd, rowNo, colNo, value16);

					}
					sqlite3_finalize(stmt);

					delete [] query8;
					delete [] value8;
				}

				delete [] value16;
			}
			break;
		}

		return CallWindowProc(cbOldEditDataEdit, hWnd, msg, wParam, lParam);
	}

	LRESULT CALLBACK cbNewAddTableCell(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_GETDLGCODE)
			return (DLGC_WANTALLKEYS | CallWindowProc(cbOldAddTableCell, hWnd, msg, wParam, lParam));

		switch(msg){
			case WM_KILLFOCUS: {
				SendMessage(hWnd, WMU_SAVE_DATA, 0, 0);
				HWND parent = GetParent(hWnd);
				DestroyWindow(hWnd);

				InvalidateRect(parent, 0, TRUE);
			}
			break;

			case WM_KEYDOWN: {
				if (wParam == VK_RETURN) {
					SendMessage(hWnd, WMU_SAVE_DATA, 0, 0);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
				}

				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WMU_SAVE_DATA: {
				HWND hListWnd = GetParent(hWnd);
				LPARAM data = GetWindowLong(hWnd, GWL_USERDATA);
				int size = GetWindowTextLength(hWnd);
				TCHAR* value16 = new TCHAR[size + 1]{0};
				GetWindowText(hWnd, value16, size + 1);
				ListView_SetItemText(hListWnd, LOWORD(data), HIWORD(data), value16);
				delete [] value16;
			}
			break;
		}

		return CallWindowProc(cbOldAddTableCell, hWnd, msg, wParam, lParam);
	}
}
