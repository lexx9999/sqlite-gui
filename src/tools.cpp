#include <stdlib.h>

#include <shobjidl.h>
#include <objidl.h>
#include <shlguid.h>
#include <shlobj.h>
#include "dmp.h"

#include "global.h"
#include "resource.h"
#include "tools.h"
#include "utils.h"
#include "dbutils.h"
#include "dialogs.h"
#include "prefs.h"

namespace tools {
	const TCHAR* DELIMITERS[4] = {TEXT(","), TEXT(";"), TEXT("\t"), TEXT("|")};
	WNDPROC cbOldEditSheetRange;

	BOOL CALLBACK cbDlgExportCSV (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hTable = GetDlgItem(hWnd, IDC_DLG_TABLENAME);
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select name from sqlite_master where type in ('table', 'view') order by 1", -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hTable, name16);
						delete [] name16;
					}
				}
				sqlite3_finalize(stmt);

				char* table8 = prefs::get("csv-export-last-table", "");
				TCHAR* table16 = utils::utf8to16(table8);
				int idx = ComboBox_FindString(hTable, 0, table16);
				ComboBox_SetCurSel(hTable, idx == -1 ? 0 : idx);
				delete [] table8;
				delete [] table16;

				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_ISCOLUMNS), BST_CHECKED);

				HWND hDelimiter = GetDlgItem(hWnd, IDC_DLG_DELIMITER);
				for (int i = 0; i < 4; i++)
					ComboBox_AddString(hDelimiter, i != 2 ? DELIMITERS[i] : TEXT("Tab"));
				ComboBox_SetCurSel(hDelimiter, prefs::get("csv-export-delimiter"));

				HWND hNewLine = GetDlgItem(hWnd, IDC_DLG_NEWLINE);
				ComboBox_AddString(hNewLine, TEXT("Windows"));
				ComboBox_AddString(hNewLine, TEXT("Unix"));
				ComboBox_SetCurSel(hNewLine, prefs::get("csv-export-is-unix-line"));

				SetFocus(hTable);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK) {
					TCHAR table16[256] = {0};
					GetDlgItemText(hWnd, IDC_DLG_TABLENAME, table16, 256);

					TCHAR path16[MAX_PATH + 1];
					_sntprintf(path16, MAX_PATH, table16);
					if (!utils::saveFile(path16, TEXT("CSV files\0*.csv\0All\0*.*\0"), TEXT("csv"), hWnd))
						return true;

					bool isColumns = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISCOLUMNS));
					int iDelimiter = ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_DELIMITER));
					bool isUnixNewLine = ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_NEWLINE));

					prefs::set("csv-export-is-columns", isColumns);
					prefs::set("csv-export-delimiter", iDelimiter);
					prefs::set("csv-export-is-unix-line", +isUnixNewLine);

					int len = _tcslen(table16) + 128;
					TCHAR query16[len + 1] = {0};
					_sntprintf(query16, len, TEXT("select * from \"%ls\""), table16);

					TCHAR err16[1024]{0};
					if (exportCSV(path16, query16, err16) != -1) {
						char* table8 = utils::utf16to8(table16);
						prefs::set("csv-export-last-table", table8);
						delete [] table8;

						EndDialog(hWnd, DLG_OK);
					} else {
						MessageBox(hWnd, err16, NULL, MB_OK);
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgExportSQL (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_DATADDL), BST_CHECKED);

				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_OBJECTLIST);
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select type, name, rowid from sqlite_master where sql is not null order by case when type = 'table' then 0 when type = 'view' then 1 when type = 'trigger' then 2 else 4 end, name", -1, &stmt, 0)) {
					ListView_SetData(hListWnd, stmt);
					ListView_SetColumnWidth(hListWnd, 0, 0);
					ListView_SetColumnWidth(hListWnd, 3, 0);
					ListView_SetColumnWidth(hListWnd, 2, LVSCW_AUTOSIZE_USEHEADER);
				}
				sqlite3_finalize(stmt);

				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_MULTIPLE_INSERT), prefs::get("sql-export-multiple-insert") ? BST_CHECKED : BST_UNCHECKED);

				SetFocus(hListWnd);
				utils::alignDialog(hWnd, hMainWnd);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK) {
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_OBJECTLIST);
					if (!ListView_GetSelectedCount(hListWnd)) {
						MessageBox(hWnd, TEXT("Please select an object to export"), NULL, MB_OK);
						return true;
					}

					TCHAR path16[MAX_PATH + 1];
					_sntprintf(path16, MAX_PATH, TEXT("script.sql"));
					if (!utils::saveFile(path16, TEXT("SQL files\0*.sql\0All\0*.*\0"), TEXT("sql"), hWnd))
						return true;

					FILE* f = _tfopen(path16, TEXT("wb"));
					if (f == NULL) {
						MessageBox(hWnd, TEXT("Error to open file"), NULL, MB_OK);
						return true;
					}

					bool isDDL = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_DATADDL)) || Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_DDLONLY));
					bool isData = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_DATADDL)) || Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_DATAONLY));
					bool isMultipleInsert = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_MULTIPLE_INSERT));
					bool rc = true;

					if (isDDL) {
						int count = ListView_GetSelectedCount(hListWnd);
						char placeholders8[count * 2]{0}; // count = 3 => ?, ?, ?
						for (int i = 0; i < count * 2 - 1; i++)
							placeholders8[i] = i % 2 ? ',' : '?';

						char sql8[128 + count * 2]{0};
						sprintf(sql8, "select sql from sqlite_master where rowid in (%s) and name <> 'sqlite_sequence'", placeholders8);

						sqlite3_stmt *stmt;
						rc = SQLITE_OK == sqlite3_prepare_v2(db, sql8, -1, &stmt, 0);
						if (rc) {
							TCHAR buf16[64]{0};
							int pos = -1;
							for (int i = 0; i < count; i++) {
								pos = ListView_GetNextItem(hListWnd, pos, LVNI_SELECTED);
								ListView_GetItemText(hListWnd, pos, 3, buf16, 128);
								sqlite3_bind_int64(stmt, i + 1, _tcstol(buf16, NULL, 10));
							}

							while (SQLITE_ROW == sqlite3_step(stmt))
								fprintf(f, "%s;\n\n", sqlite3_column_text(stmt, 0));
						}
						sqlite3_finalize(stmt);
					}

					if (rc && isData) {
						int pos = -1;
						while((pos = ListView_GetNextItem(hListWnd, pos, LVNI_SELECTED)) != -1) {
							TCHAR type16[64];
							TCHAR table16[64];
							ListView_GetItemText(hListWnd, pos, 1, type16, 64);
							ListView_GetItemText(hListWnd, pos, 2, table16, 64);
							if (_tcscmp(type16, TEXT("table")))
								continue;

							sqlite3_stmt *stmt;
							char* table8 = utils::utf16to8(table16);
							char sql8[] = "select 'select ' || quote('insert into \"' || ?1 || '\" (\"' || group_concat(name, '\", \"') || '\") values (') || '||' || " \
								"group_concat('quote(\"' || name || '\")', '|| '', '' || ') || '|| '');'' || char(10) from \"' || ?1 || '\"' " \
								"from pragma_table_info(?1) order by cid";
							char sql8m[] = "select 'select ' || quote('insert into \"' || ?1 || '\" (\"' || group_concat(name, '\", \"') || '\") values ') || ' || group_concat(char(10) || ''(''||' ||  " \
								"group_concat('quote(\"' || name || '\")', '|| '', '' || ') || '|| '')'', '', '') || '';'' || char(10) from \"' || ?1 || '\"' " \
								"from pragma_table_info(?1) order by cid";


							if (SQLITE_OK == sqlite3_prepare_v2(db, isMultipleInsert ? sql8m : sql8, -1, &stmt, 0)) {
								sqlite3_bind_text(stmt, 1, table8, strlen(table8),  SQLITE_TRANSIENT);

								if (SQLITE_ROW == sqlite3_step(stmt)) {
									sqlite3_stmt *stmt2;
									if (SQLITE_OK == sqlite3_prepare_v2(db, (const char*)sqlite3_column_text(stmt, 0), -1, &stmt2, 0)) {
										int rowNo = 0;
										fprintf(f, "-- %s\n", table8);

										while (SQLITE_ROW == sqlite3_step(stmt2)) {
											const char* row8 = (const char*)sqlite3_column_text(stmt2, 0);
											if (row8) {
												fprintf(f, row8);
												rowNo++;
											}
										}

										if (!isMultipleInsert || rowNo == 0)
											fprintf(f, "-- %i rows\n\n", rowNo);
										else
											fprintf(f, "\n\n");
									}
									sqlite3_finalize(stmt2);
								}
							}
							sqlite3_finalize(stmt);

							delete [] table8;
						}
					}
					fclose(f);

					if (rc) {
						prefs::set("sql-export-multiple-insert", isMultipleInsert);
						EndDialog(hWnd, DLG_OK);
					} else {
						showDbError(hWnd);
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	TCHAR* csvReadLine(FILE* f) {
		size_t size = 32000, bsize = 2000;
		TCHAR* line = new TCHAR[size + 1] {0};
		TCHAR buf[bsize + 1]{0};
		int qCount = 0;

		while (!feof(f)) {
			if (_fgetts(buf, bsize + 1, f)) {
				if (_tcslen(line) + bsize > size) {
					size *= 2;
					line = (TCHAR*)realloc(line, size + 1);
				}
				_tcscat(line, buf);

				for (size_t i = 0; i < _tcslen(buf); i++)
					qCount += buf[i] == TEXT('"');

				if ((_tcslen(buf) < bsize) && (qCount % 2 == 0))
					break;
			} else {
				break;
			}
		}

		return line;
	}

	// lParam = in path
	BOOL CALLBACK cbDlgImportCSV (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_ISCOLUMNS), prefs::get("csv-import-is-columns") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_TRIM_VALUES), prefs::get("csv-import-trim-values") ? BST_CHECKED : BST_UNCHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_SKIP_EMPTY), prefs::get("csv-import-skip-empty") ? BST_CHECKED : BST_UNCHECKED);

				HWND hDelimiter = GetDlgItem(hWnd, IDC_DLG_DELIMITER);
				for (int i = 0; i < 4; i++)
					ComboBox_AddString(hDelimiter, i != 2 ? DELIMITERS[i] : TEXT("Tab"));
				ComboBox_SetCurSel(hDelimiter, prefs::get("csv-import-delimiter"));

				HWND hEncoding = GetDlgItem(hWnd, IDC_DLG_ENCODING);
				ComboBox_AddString(hEncoding, TEXT("UTF-8")); // CP_UTF8
				ComboBox_AddString(hEncoding, TEXT("ANSI")); // CP_ACP
				ComboBox_SetCurSel(hEncoding, prefs::get("csv-import-encoding"));

				TCHAR name16[256];
				_tsplitpath((TCHAR*)lParam, NULL, NULL, name16, NULL);
				for(int i = 0; name16[i]; i++)
					name16[i] = _totlower(name16[i]);

				_tcscat(name16, TEXT("_tmp"));
				SetDlgItemText(hWnd, IDC_DLG_TABLENAME, name16);
				SetWindowLongPtr(hWnd, GWLP_USERDATA, lParam);

				SendMessage(hWnd, WMU_SOURCE_UPDATED, 1, 0);
				SetFocus(GetDlgItem(hWnd, IDC_DLG_TABLENAME));
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_IMPORT_ACTION), BST_CHECKED);
				Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_ISREPLACE), BST_CHECKED);

				utils::alignDialog(hWnd, hMainWnd);
			}
			break;

			// wParam = init flag to auto-detect separator
			case WMU_SOURCE_UPDATED: {
				const TCHAR* delimiter; // is defined on first line
				int isUTF8 = ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_ENCODING)) == 0;
				bool isColumns = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISCOLUMNS));
				HWND hPreviewWnd = GetDlgItem(hWnd, IDC_DLG_PREVIEW);

				TCHAR* path16 = (TCHAR*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

				FILE* f = _tfopen(path16, isUTF8 ? TEXT("r, ccs=UTF-8") : TEXT("r"));
				if (f == NULL) {
					MessageBox(hWnd, TEXT("Error to open file"), NULL, MB_OK);
					return true;
				}

				ListView_Reset(hPreviewWnd);

				auto addCell = [hPreviewWnd, isColumns] (int lineNo, int colNo, TCHAR* column) {
					if (lineNo == 0) {
						LVCOLUMN lvc;
						lvc.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;
						lvc.iSubItem = colNo;
						if (isColumns) {
							lvc.pszText = (TCHAR*)column;
							lvc.cchTextMax = _tcslen(column) + 1;
						} else {
							TCHAR name[64];
							_sntprintf(name, 63, TEXT("Column%i"), colNo);
							lvc.pszText = name;
							lvc.cchTextMax = 64;
						}
						lvc.cx = 50;
						ListView_InsertColumn(hPreviewWnd, colNo, &lvc);
					}

					if ((isColumns && lineNo > 0) || !isColumns) {
						LVITEM  lvi = {0};
						lvi.mask = LVIF_TEXT;
						lvi.iSubItem = colNo;
						lvi.iItem = isColumns ? lineNo - 1 : lineNo;
						lvi.pszText = column;
						lvi.cchTextMax = _tcslen(column) + 1;
						if (colNo == 0)
							ListView_InsertItem(hPreviewWnd, &lvi);
						else
							ListView_SetItem(hPreviewWnd, &lvi);
					}
				};

				int lineNo = 0;
				while(!feof (f) && lineNo < 5) {
					TCHAR* line = csvReadLine(f);
					int colNo = 0;

					if (lineNo == 0) {
						// delimiter auto-detection
						if (wParam == 1) {
							int delimCount = 4;
							int dCount[delimCount]{0};
							int maxCount = 0;
							bool inQuote = false;
							for (int pos = 0; pos < (int)_tcslen(line); pos++) {
								TCHAR c = line[pos];

								if (c == TEXT('"'))
									inQuote = !inQuote;

								for (int delimNo = 0; delimNo < delimCount && !inQuote; delimNo++) {
									dCount[delimNo] += line[pos] == DELIMITERS[delimNo][0];
									maxCount = maxCount < dCount[delimNo] ? dCount[delimNo] : maxCount;
								}
							}

							for (int delimNo = 0; delimNo < delimCount; delimNo++) {
								if (dCount[delimNo] == maxCount) {
									ComboBox_SetCurSel(GetDlgItem(hWnd, IDC_DLG_DELIMITER), delimNo);
									break;
								}
							}
						}

						delimiter = DELIMITERS[ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_DELIMITER))];
					}

					TCHAR value[_tcslen(line)];
					bool inQuotes = false;
					int valuePos = 0;
					int i = 0;
					do {
						value[valuePos++] = line[i];

						if ((!inQuotes && (line[i] == delimiter[0] || line[i] == TEXT('\n'))) || !line[i + 1]) {
							value[valuePos - (line[i + 1] != 0 || inQuotes)] = 0;
							valuePos = 0;
							addCell(lineNo, colNo, value);
							colNo++;
						}

						if (line[i] == TEXT('"') && line[i + 1] != TEXT('"')) {
							valuePos--;
							inQuotes = !inQuotes;
						}

						if (line[i] == TEXT('"') && line[i + 1] == TEXT('"'))
							i++;
					} while (line[++i]);

					lineNo++;
					delete [] line;
				}

				fclose(f);
				ListView_SetExtendedListViewStyle(hPreviewWnd, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
				for (int colNo = 0; colNo < ListView_GetColumnCount(hPreviewWnd); colNo++)
					ListView_SetColumnWidth(hPreviewWnd, colNo, LVSCW_AUTOSIZE);


				HWND hHeader = ListView_GetHeader(hPreviewWnd);
				SetWindowTheme(hHeader, TEXT(" "), TEXT(" "));

				if (Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_IMPORT_ACTION2)) == BST_CHECKED) {
					HWND hTablesWnd = GetDlgItem(hWnd, IDC_DLG_TABLENAMES);
					TCHAR tblname16[256];
					GetWindowText(hTablesWnd, tblname16, 255);

					ComboBox_ResetContent(hTablesWnd);
					int colCount = Header_GetItemCount(ListView_GetHeader(hPreviewWnd));
					char sql8[] = "select sm.name from sqlite_master sm, pragma_table_info(sm.name) ti " \
						"where sm.type = 'table' and sm.name not like 'sqlite_%' " \
						"group by sm.name having count(1) = ?1 order by 1";
					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db, sql8, -1, &stmt, 0)) {
						sqlite3_bind_int(stmt, 1, colCount);
						while (SQLITE_ROW == sqlite3_step(stmt)) {
							TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
							ComboBox_AddString(hTablesWnd, name16);
							delete [] name16;
						}
					}
					sqlite3_finalize(stmt);

					int pos = MAX(ComboBox_FindStringExact(hTablesWnd, 0, tblname16), 0);
					ComboBox_SetCurSel(hTablesWnd, pos);
					GetWindowText(hTablesWnd, tblname16, 255);

					if (SQLITE_OK == sqlite3_prepare_v2(db, "select name from pragma_table_info(?1)", -1, &stmt, 0)) {
						char* tblname8 = utils::utf16to8(tblname16);
						sqlite3_bind_text(stmt, 1, tblname8, strlen(tblname8),  SQLITE_TRANSIENT);
						delete [] tblname8;

						int colNo = 0;
						while (SQLITE_ROW == sqlite3_step(stmt)) {
							TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
							Header_SetItemText(hHeader, colNo, name16);
							delete [] name16;
							colNo++;
						}
					}
					sqlite3_finalize(stmt);
				}
			}
			break;

			case WM_COMMAND: {
				WORD id = LOWORD(wParam);
				WORD cmd = HIWORD(wParam);
				if (cmd == BN_CLICKED && id == IDC_DLG_IMPORT_ACTION) {
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_TABLENAME), SW_SHOW);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_TABLENAMES), SW_HIDE);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_ISTRUNCATE), SW_HIDE);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_ISREPLACE), SW_HIDE);
					SendMessage(hWnd, WMU_SOURCE_UPDATED, 0, 0);
				}

				if (cmd == BN_CLICKED && id == IDC_DLG_IMPORT_ACTION2) {
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_TABLENAME), SW_HIDE);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_TABLENAMES), SW_SHOW);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_ISTRUNCATE), SW_SHOW);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_ISREPLACE), SW_SHOW);
					SendMessage(hWnd, WMU_SOURCE_UPDATED, 0, 0);
				}

				if ((cmd == CBN_SELCHANGE && id == IDC_DLG_ENCODING) ||
					(cmd == CBN_SELCHANGE && id == IDC_DLG_DELIMITER) ||
					(cmd == BN_CLICKED && id == IDC_DLG_ISCOLUMNS))
					SendMessage(hWnd, WMU_SOURCE_UPDATED, 0, 0);

				if (wParam == IDC_DLG_OK) {
					bool isCreateTable = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_IMPORT_ACTION)) == BST_CHECKED;
					bool isTruncate = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISTRUNCATE)) == BST_CHECKED;
					if (isTruncate && MessageBox(hWnd, TEXT("All data from table will be erased. Continue?"), TEXT("Confirmation"), MB_OKCANCEL | MB_ICONASTERISK) != IDOK)
						return true;

					TCHAR tblname16[256]{0};
					GetDlgItemText(hWnd, isCreateTable ? IDC_DLG_TABLENAME : IDC_DLG_TABLENAMES, tblname16, 255);

					prefs::set("csv-import-encoding", ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_ENCODING)));
					prefs::set("csv-import-delimiter", ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_DELIMITER)));
					prefs::set("csv-import-is-columns", +Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISCOLUMNS)));
					prefs::set("csv-import-is-create-table", isCreateTable);
					prefs::set("csv-import-is-truncate", isTruncate);
					prefs::set("csv-import-is-replace", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISREPLACE)) == BST_CHECKED);
					prefs::set("csv-import-trim-values", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_TRIM_VALUES)) == BST_CHECKED);
					prefs::set("csv-import-skip-empty", Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_SKIP_EMPTY)) == BST_CHECKED);

					TCHAR err[1024]{0};
					int rowCount = importCSV((TCHAR*)GetWindowLongPtr(hWnd, GWLP_USERDATA), tblname16, err);
					if (rowCount != -1) {
						_sntprintf((TCHAR*)GetWindowLongPtr(hWnd, GWLP_USERDATA), MAX_PATH, TEXT("%ls"), tblname16);
						EndDialog(hWnd, rowCount);
					} else {
						MessageBox(hWnd, err, NULL, 0);
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, -1);
			}
			break;
		}

		return false;
	}

	// lParam = in file
	BOOL CALLBACK cbDlgImportJSON (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				SetWindowLongPtr(hWnd, GWLP_USERDATA, lParam);

				TCHAR* name16 = utils::getFileName((TCHAR*)lParam, true);
				SetDlgItemText(hWnd, IDC_DLG_TABLENAME, name16);
				delete [] name16;
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK || wParam == IDOK) {
					TCHAR table16[256];
					GetDlgItemText(hWnd, IDC_DLG_TABLENAME, table16, 255);

					char* path8 = utils::utf16to8((TCHAR*)GetWindowLongPtr(hWnd, GWLP_USERDATA));
					char* data8 = utils::readFile(path8);
					delete [] path8;
					if (data8 == 0) {
						MessageBox(hWnd, TEXT("File is empty"), TEXT("Info"), MB_OK);
						EndDialog(hWnd, DLG_CANCEL);
						return true;
					}

					sqlite3_stmt* stmt;
					bool rc = SQLITE_OK == sqlite3_prepare_v2(db,
							"with t as (select value from json_each(?1) limit 1) " \
							"select " \
							"'create table \"' || ?2 || '\" (' || group_concat('\"' || e.key || '\" '|| e.type, ',') || ')' crt, " \
							"'insert into \"' || ?2 || '\" select ' || group_concat(\"json_extract(value, '$.\" || replace(e.key, '''', '''''') || \"') \", ',') || ' from json_each(\?1)' ins "
							"from t, json_each(t.value) e;", -1, &stmt, 0);
					if (rc) {
						char* table8 = utils::utf16to8(table16);
						sqlite3_bind_text(stmt, 1, data8, strlen(data8),  SQLITE_TRANSIENT);
						sqlite3_bind_text(stmt, 2, table8, strlen(table8),  SQLITE_TRANSIENT);

						delete [] table8;
						rc = SQLITE_ROW == sqlite3_step(stmt);
						if (rc) {
							const  char* create8 = (const char*)sqlite3_column_text(stmt, 0);
							const  char* insert8 = (const char*)sqlite3_column_text(stmt, 1);
							rc = SQLITE_OK == sqlite3_exec(db, create8, 0, 0, 0);
							if (rc) {
								sqlite3_stmt* stmt2;
								if (SQLITE_OK == sqlite3_prepare_v2(db, insert8, -1, &stmt2, 0)) {
									sqlite3_bind_text(stmt2, 1, data8, strlen(data8),  SQLITE_TRANSIENT);
									rc = SQLITE_DONE == sqlite3_step(stmt2);
								} else
									showDbError(hWnd);

								sqlite3_finalize(stmt2);
							}
						}
					}

					if (!rc)
						showDbError(hWnd);

					sqlite3_finalize(stmt);

					if (rc) {
						_sntprintf((TCHAR*)GetWindowLongPtr(hWnd, GWLP_USERDATA), MAX_PATH, table16);
						EndDialog(hWnd, DLG_OK);
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	LRESULT CALLBACK cbNewEditSheetRange(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_GETDLGCODE: {
				return DLGC_WANTALLKEYS | CallWindowProc(cbOldEditSheetRange, hWnd, msg, wParam, lParam);
			}
			break;

			case WM_KEYDOWN: {
				if (wParam == VK_RETURN) {
					SendMessage(GetParent(hWnd), WMU_UPDATE_SHEET_PREVIEW, 0, 0);
					return true;
				}
			}
			break;
		}

		return CallWindowProc(cbOldEditSheetRange, hWnd, msg, wParam, lParam);
	}

	// lParam = USERDATA = tablename
	BOOL CALLBACK cbDlgImportSheet (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				SetWindowLongPtr(hWnd, GWLP_USERDATA, lParam);
				cbOldEditSheetRange = (WNDPROC)SetWindowLongPtr(GetDlgItem(hWnd, IDC_DLG_SHEET_RANGE), GWLP_WNDPROC, (LONG_PTR)cbNewEditSheetRange);
				PostMessage(hWnd, WMU_UPDATE_SHEET_IDS, 0, 0);
			}
			break;

			case WM_COMMAND: {
				HWND hSheetIdWnd = GetDlgItem(hWnd, IDC_DLG_SHEET_ID);
				HWND hSheetNameWnd = GetDlgItem(hWnd, IDC_DLG_SHEET_NAME);

				if (LOWORD(wParam) == IDC_DLG_ISCOLUMNS && HIWORD(wParam) == BN_CLICKED)
					PostMessage(hWnd, WMU_UPDATE_SHEET_PREVIEW, 0, 0);

				if (LOWORD(wParam) == IDC_DLG_SHEET_NAME && HIWORD(wParam) == CBN_SELCHANGE)
					PostMessage(hWnd, WMU_UPDATE_SHEET_PREVIEW, 0, 0);

				if (LOWORD(wParam) == IDC_DLG_SHEET_ID && HIWORD(wParam) == CBN_SELCHANGE) {
					TCHAR sheetId16[1024]{0};

					ComboBox_ResetContent(hSheetNameWnd);

					int idx = ComboBox_GetCurSel(hSheetIdWnd);
					if (idx != 0) {
						ComboBox_GetLBText(hSheetIdWnd, idx, sheetId16);
					} else {
						TCHAR* clipboard16 = utils::getClipboardText();
						TCHAR* pos16 = _tcsstr(clipboard16, TEXT("/d/"));
						if (pos16) {
							pos16 += 3;

							int i = 0;
							while (pos16[i] != 0 && pos16[i] != TEXT('/')) {
								sheetId16[i] = pos16[i];
								i++;
							}
						} else {
							_tcsncpy(sheetId16, clipboard16, 1023);
						}

						delete [] clipboard16;
					}
					ComboBox_SetCurSel(hSheetIdWnd, -1);

					char path8[1024];
					char* sheetId8 = utils::utf16to8(sheetId16);
					char* googleApiKey8 = prefs::get("google-api-key", "");
					sprintf(path8, "v4/spreadsheets/%s?key=%s", sheetId8, googleApiKey8);
					delete [] sheetId8;
					delete [] googleApiKey8;

					char* data8 = utils::httpRequest("GET", "sheets.googleapis.com", path8);
					if (data8) {
						sqlite3_stmt *stmt;
						if (SQLITE_OK == sqlite3_prepare_v2(db, "select json_extract(value, '$.properties.title') from json_each(?1, '$.sheets')", -1, &stmt, 0)) {
							sqlite3_bind_text(stmt, 1, data8, strlen(data8),  SQLITE_TRANSIENT);
							while (SQLITE_ROW == sqlite3_step(stmt)) {
								TCHAR* sheet16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
								ComboBox_AddString(hSheetNameWnd, sheet16);
								delete [] sheet16;
							}
						}
						sqlite3_finalize(stmt);
						delete [] data8;
					}

					if (ComboBox_GetCount(hSheetNameWnd) > 0) {
						sqlite3_stmt *stmt;
						if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "replace into sheets (sheet_id, \"time\") values (?1, ?2)", -1, &stmt, 0)) {
							char* sheetId8 = utils::utf16to8(sheetId16);
							sqlite3_bind_text(stmt, 1, sheetId8, strlen(sheetId8),  SQLITE_TRANSIENT);
							delete [] sheetId8;
							sqlite3_bind_int(stmt, 2, std::time(0));
							sqlite3_step(stmt);
						}
						sqlite3_finalize(stmt);
						SendMessage(hWnd, WMU_UPDATE_SHEET_IDS, 0, 0);
						int idx = ComboBox_FindStringExact(hSheetIdWnd, 0, sheetId16);
						ComboBox_SetCurSel(hSheetIdWnd, idx);

						ComboBox_SetCurSel(hSheetNameWnd, 0);
						SetFocus(hSheetNameWnd);

						PostMessage(hWnd, WMU_UPDATE_SHEET_PREVIEW, 0, 0);
					} else {
						TCHAR msg16[2048]{0};
						_sntprintf(msg16, 2047, TEXT("The ID or url is invalid:\n%ls\n\nThe spreadsheets should be public with \"Anyone with the link\"-access."), sheetId16);
						MessageBox(hWnd, msg16, NULL, MB_OK);
					}
				}

				if (wParam == IDC_DLG_OK || wParam == IDOK) {
						TCHAR table16[1024];
						GetDlgItemText(hWnd, IDC_DLG_TABLENAME, table16, 1023);

						if (_tcslen(table16) == 0)
							return MessageBox(hWnd, TEXT("The target table is empty"), NULL, MB_OK);

						TCHAR* schema16 = utils::getTableName(table16, true);
						TCHAR* tablename16 = utils::getTableName(table16, false);

						TCHAR query16[2048];
						_sntprintf(query16, 2047, TEXT("create table \"%ls\".\"%ls\" as select * from temp.googlesheet"), schema16, tablename16);
						char* query8 = utils::utf16to8(query16);

						bool rc = SQLITE_OK == sqlite3_exec(db, query8, NULL, NULL, NULL);

						if (rc) {
							TCHAR* outname16 = (TCHAR*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
							if (_tcscmp(schema16, TEXT("main")) == 0)
								_tcscpy(outname16, tablename16);
						}

						delete [] schema16;
						delete [] tablename16;
						delete [] query8;

						if (rc) {
							EndDialog(hWnd, DLG_OK);
						} else
							showDbError(hWnd);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WMU_UPDATE_SHEET_IDS: {
				HWND hSheetIdWnd = GetDlgItem(hWnd, IDC_DLG_SHEET_ID);

				ComboBox_ResetContent(hSheetIdWnd);
				ComboBox_AddString(hSheetIdWnd, TEXT("<<Paste spreadsheets ID or url from clipboard>>"));

				sqlite3_stmt *stmt;
				BOOL rc = SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select sheet_id from sheets order by \"time\" desc limit 20", -1, &stmt, 0);
				while (rc && SQLITE_ROW == sqlite3_step(stmt)) {
					TCHAR* sheetId16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
					ComboBox_AddString(hSheetIdWnd, sheetId16);
					delete [] sheetId16;
				}
				sqlite3_finalize(stmt);
			}
			break;

			case WMU_UPDATE_SHEET_PREVIEW: {
				HWND hPreviewWnd = GetDlgItem(hWnd, IDC_DLG_PREVIEW);
				ListView_Reset(hPreviewWnd);

				TCHAR sheetId16[1024];
				GetDlgItemText(hWnd, IDC_DLG_SHEET_ID, sheetId16, 1023);

				TCHAR sheet16[1024];
				GetDlgItemText(hWnd, IDC_DLG_SHEET_NAME, sheet16, 1023);

				TCHAR range16[1024];
				GetDlgItemText(hWnd, IDC_DLG_SHEET_RANGE, range16, 1023);

				bool isColumns = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_ISCOLUMNS));

				char path8[2048];
				char* sheetId8 = utils::utf16to8(sheetId16);
				char* sheet8 = utils::utf16to8(sheet16);
				char* range8 = utils::utf16to8(range16);
				char* googleApiKey8 = prefs::get("google-api-key", "");
				sprintf(path8, "v4/spreadsheets/%s/values/%s%s%s?key=%s", sheetId8, sheet8, strlen(range8) ? "!" : "", range8, googleApiKey8);
				delete [] sheetId8;
				delete [] sheet8;
				delete [] range8;
				delete [] googleApiKey8;

				char* create8 = new char[MAX_TEXT_LENGTH]{0};
				char* data8 = utils::httpRequest("GET", "sheets.googleapis.com", path8);
				if (data8) {
					if (SQLITE_OK != sqlite3_exec(db, "drop table if exists temp.googlesheet", NULL, NULL, NULL))
						showDbError(hWnd);

					int colCount = 0;
					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db, isColumns ?
							"select json_array_length(t.value), '\"' || group_concat(t2.value, '\", \"') || '\"' from json_each(t.value, '$') t2, json_each(?1, '$.values') t where t.key = 0" :
							"select max(json_array_length(t.value)), '\"col' || group_concat(t2.key, '\", \"col') || '\"' from json_each(t.value, '$') t2, json_each(?1, '$.values') t group by t.key order by 1 desc limit 1",
							-1, &stmt, 0)) {
						sqlite3_bind_text(stmt, 1, data8, strlen(data8),  SQLITE_TRANSIENT);
						sqlite3_step(stmt);
						colCount = sqlite3_column_int(stmt, 0);
						const char* columns = (const char*)sqlite3_column_text(stmt, 1);

						if (colCount > 0) {
							sprintf(create8, "create table temp.googlesheet as with res (%s) as (select ", columns);
							for (int colNo = 0; colNo < colCount; colNo++) {
								char buf8[64]{0};
								sprintf(buf8, " json_extract(value, '$[%i]')", colNo);
								strcat(create8, buf8);
								if (colNo != colCount - 1)
									strcat(create8, ", ");
							}
							strcat(create8, " from json_each(?1, '$.values')");
							if (isColumns)
								strcat(create8, " where key > 0");
							strcat(create8, ") select * from res");
						} else {
							MessageBox(hWnd, TEXT("The sheet is empty"), NULL, MB_OK);
						}
					}
					sqlite3_finalize(stmt);

					if (SQLITE_OK == sqlite3_prepare_v2(db, create8, -1, &stmt, 0)) {
						sqlite3_bind_text(stmt, 1, data8, strlen(data8),  SQLITE_TRANSIENT);
						sqlite3_step(stmt);
					}
					sqlite3_finalize(stmt);

					if (SQLITE_OK == sqlite3_prepare_v2(db, "select * from temp.googlesheet", -1, &stmt, 0)) {
						sqlite3_bind_text(stmt, 1, data8, strlen(data8),  SQLITE_TRANSIENT);
						ListView_SetData(hPreviewWnd, stmt);
					}
					sqlite3_finalize(stmt);

					delete [] create8;
					delete [] data8;
				} else {
					MessageBox(hWnd, TEXT("Can't fetch data"), NULL, MB_OK);
				}
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgExportJSON (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hTable = GetDlgItem(hWnd, IDC_DLG_TABLENAME);
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select name from sqlite_master where type in ('table', 'view') order by 1", -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hTable, name16);
						delete [] name16;
					}
				}
				sqlite3_finalize(stmt);
				ComboBox_SetCurSel(hTable, 0);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK || wParam == IDOK) {
					TCHAR table16[256] = {0};
					GetDlgItemText(hWnd, IDC_DLG_TABLENAME, table16, 256);

					TCHAR path16[MAX_PATH + 1];
					_sntprintf(path16, MAX_PATH, table16);
					if (!utils::saveFile(path16, TEXT("JSON files\0*.json\0All\0*.*\0"), TEXT("json"), hWnd))
						return true;

					FILE* f = _tfopen(path16, TEXT("wb"));
					if (f == NULL) {
						MessageBox(hWnd, TEXT("Unable to open target file"), NULL, MB_OK);
						return true;
					}

					sqlite3_stmt* stmt;
					bool rc = SQLITE_OK == sqlite3_prepare_v2(db,
							"select 'select json_group_array(json_object(' || group_concat('''' || name || ''', iif(typeof(\"' || name || '\") <> ''blob'', \"' || name || '\", ''(BLOB)'')', ', ') || ')) from \"' || ?1 || '\"' " \
							"from pragma_table_info(?1) order by cid", -1, &stmt, 0);
					if (rc) {
						char* table8 = utils::utf16to8(table16);
						sqlite3_bind_text(stmt, 1, table8, strlen(table8),  SQLITE_TRANSIENT);
						delete [] table8;

						if (SQLITE_ROW == sqlite3_step(stmt)) {
							sqlite3_stmt* stmt2;
							if (SQLITE_OK == sqlite3_prepare_v2(db, (const char*)sqlite3_column_text(stmt, 0), -1, &stmt2, 0)) {
								rc = SQLITE_ROW == sqlite3_step(stmt2);
								if (rc)
									fprintf(f, (const char*)sqlite3_column_text(stmt2, 0));
							} else
								showDbError(hWnd);

							sqlite3_finalize(stmt2);

						}
					}
					sqlite3_finalize(stmt);
					fclose(f);

					if (rc) {
						EndDialog(hWnd, DLG_OK);
					} else {
						showDbError(hWnd);
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgExportExcel (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				SetWindowText(hWnd, TEXT("Export data of table/view to Excel file"));

				HWND hTable = GetDlgItem(hWnd, IDC_DLG_TABLENAME);
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select name from sqlite_master where type in ('table', 'view') order by 1", -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hTable, name16);
						delete [] name16;
					}
				}
				sqlite3_finalize(stmt);
				ComboBox_SetCurSel(hTable, 0);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK || wParam == IDOK) {
					TCHAR table16[256] = {0};
					GetDlgItemText(hWnd, IDC_DLG_TABLENAME, table16, 256);

					TCHAR path16[MAX_PATH + 1];
					_sntprintf(path16, MAX_PATH, table16);
					if (!utils::saveFile(path16, TEXT("Excel files\0*.xlsx\0All\0*.*\0"), TEXT("xlsx"), hWnd))
						return true;

					int len = _tcslen(table16) + 128;
					TCHAR query16[len + 1] = {0};
					_sntprintf(query16, len, TEXT("select * from \"%ls\""), table16);

					if (exportExcel(path16, query16))
						EndDialog(hWnd, DLG_OK);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	// USERDATA: 0 - import 1 - export
	BOOL CALLBACK cbDlgExportImportODBC (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				bool isExport = lParam;
				SetWindowLongPtr(hWnd, GWLP_USERDATA, isExport);
				SetWindowText(hWnd, isExport ? TEXT("Export data via ODBC") : TEXT("Import data via ODBC"));
				SetDlgItemText(hWnd, IDC_DLG_ODBC_SCHEMA_LABEL, isExport ? TEXT("Source schema") : TEXT("Import to schema"));
				SetDlgItemText(hWnd, IDC_DLG_OK, isExport ? TEXT("Export table(s)") : TEXT("Import table(s)"));

				HWND hStrategyWnd = GetDlgItem(hWnd, IDC_DLG_ODBC_STRATEGY);
				ComboBox_AddString(hStrategyWnd, TEXT("Do nothing"));
				ComboBox_AddString(hStrategyWnd, TEXT("Skip"));
				ComboBox_AddString(hStrategyWnd, TEXT("Clear existing data"));
				ComboBox_AddString(hStrategyWnd, TEXT("Drop and create new table"));
				ComboBox_SetCurSel(hStrategyWnd, prefs::get("odbc-strategy"));

				HWND hSchemaWnd = GetDlgItem(hWnd, IDC_DLG_ODBC_SCHEMA);
				sqlite3_stmt* stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select name from pragma_database_list where name <> 'temp' order by iif(name = 'main', 0, name)", -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* schema16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hSchemaWnd, schema16);
						delete [] schema16;
					}
				}
				sqlite3_finalize(stmt);
				ComboBox_SetCurSel(hSchemaWnd, 0);

				if (isExport)
					SendMessage(hWnd, WMU_TARGET_CHANGED, 0, 0);
			}
			break;

			case WM_COMMAND: {
				bool isExport = GetWindowLongPtr(hWnd, GWLP_USERDATA);

				if (LOWORD(wParam) == IDC_DLG_CONNECTION_STRING && HIWORD(wParam) == CBN_SELCHANGE)
					PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_DLG_CONNECTION_STRING, CBN_EDITCHANGE), (LPARAM)GetDlgItem(hWnd, IDC_DLG_CONNECTION_STRING));

				if (LOWORD(wParam) == IDC_DLG_CONNECTION_STRING && HIWORD(wParam) == CBN_DROPDOWN) {
					HWND hCSWnd = GetDlgItem(hWnd, IDC_DLG_CONNECTION_STRING);
					int size = ComboBox_GetTextLength(hCSWnd) + 1;
					TCHAR cs[size];
					ComboBox_GetText(hCSWnd, cs, size);
					ComboBox_ResetContent(hCSWnd);
					ComboBox_SetText(hCSWnd, cs);

					sqlite3_stmt *stmt;
					BOOL rc = SQLITE_OK == sqlite3_prepare_v2(db, "select 'DSN=' || dsn from odbc_dsn", -1, &stmt, 0);
					while (rc && SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* connectionString16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hCSWnd, connectionString16);
						delete [] connectionString16;
					}
					sqlite3_finalize(stmt);
				}

				if (LOWORD(wParam) == IDC_DLG_ODBC_SCHEMA && HIWORD(wParam) == CBN_SELCHANGE && isExport)
					SendMessage(hWnd, WMU_TARGET_CHANGED, 0, 0);

				if (LOWORD(wParam) == IDC_DLG_ODBC_MANAGER) {
					TCHAR winPath[MAX_PATH + 1], appPath[MAX_PATH + 1];
					GetWindowsDirectory(winPath, MAX_PATH);
					#if GUI_PLATFORM == 32
					_sntprintf(appPath, MAX_PATH, TEXT("%ls/SysWOW64/odbcad32.exe"), winPath);
					#else
					_sntprintf(appPath, MAX_PATH, TEXT("%ls/system32/odbcad32.exe"), winPath);
					#endif
					ShellExecute(0, 0, appPath, 0, 0, SW_SHOW);
					SetFocus(0);
					return 0;
				}

				if (LOWORD(wParam) == IDC_DLG_CONNECTION_STRING && (HIWORD(wParam) == CBN_EDITCHANGE) && !isExport) {
					TCHAR connectionString16[1024]{0};
					GetDlgItemText(hWnd, IDC_DLG_CONNECTION_STRING, connectionString16, 1024);

					if (!_tcslen(connectionString16))
						return 0;

					if (SQLITE_OK != sqlite3_exec(db, "drop table if exists temp.odbc_tables", NULL, NULL, NULL))
						return showDbError(hWnd);

					bool rc = false;
					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db, "select odbc_read(?1, 'TABLES', 'temp.odbc_tables')", -1, &stmt, 0)) {
						char* connectionString8 = utils::utf16to8(connectionString16);
						sqlite3_bind_text(stmt, 1, connectionString8, strlen(connectionString8), SQLITE_TRANSIENT);
						delete [] connectionString8;

						rc = SQLITE_ROW == sqlite3_step(stmt);
					}
					sqlite3_finalize(stmt);

					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_TABLES);
					ListView_Reset(hListWnd);
					if (rc) {
						if (SQLITE_OK == sqlite3_prepare_v2(db, "select table_name from temp.odbc_tables where table_type in ('TABLE', 'VIEW', 'SYSTEM TABLE') order by 1", -1, &stmt, 0)) {
							ListView_SetData(hListWnd, stmt);
							ListView_SetColumnWidth(hListWnd, 1, 290);
						}
						sqlite3_finalize(stmt);
					}
				}

				if (wParam == IDC_DLG_HELP) {
					TCHAR buf[MAX_TEXT_LENGTH];
					LoadString(GetModuleHandle(NULL), IDS_ODBC_HELP, buf, MAX_TEXT_LENGTH);
					MessageBox(hWnd, buf, TEXT("ODBC Help"), MB_OK);
				}

				if (wParam == IDC_DLG_OK) {
					HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_TABLES);
					if (ListView_GetSelectedCount(hListWnd) == 0)
						return MessageBox(0, TEXT("You should specify at least one table"), NULL, 0);

					TCHAR connectionString16[1024];
					GetDlgItemText(hWnd, IDC_DLG_CONNECTION_STRING, connectionString16, 1024);
					if (_tcslen(connectionString16) == 0)
						return MessageBox(0, TEXT("You should provide connection string"), NULL, 0);

					char* connectionString8 = utils::utf16to8(connectionString16);

					TCHAR result16[MAX_TEXT_LENGTH]{0};

					// 0 - do nothing, 1 - skip, 2 - clear, 3 - drop
					int strategy = ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_ODBC_STRATEGY));

					int rowNo = -1;
					int rc = true;
					while((rowNo = ListView_GetNextItem(hListWnd, rowNo, LVNI_SELECTED)) != -1) {
						TCHAR table16[1024];
						ListView_GetItemText(hListWnd, rowNo, 1, table16, 1023);
						char* table8 = utils::utf16to8(table16);

						TCHAR schema16[255];
						GetDlgItemText(hWnd, IDC_DLG_ODBC_SCHEMA, schema16, 255);
						char* schema8 = utils::utf16to8(schema16);

						int len = _tcslen(table16) + 1024;
						TCHAR res16[len + 1]{0};

						if (strategy && !isExport) {
							bool isExists = false;
							sqlite3_stmt* stmt;
							if (SQLITE_OK == sqlite3_prepare_v2(db, "select 1 from sqlite_master where tbl_name = ?1 ", -1, &stmt, 0)) {
								sqlite3_bind_text(stmt, 1, table8, strlen(table8), SQLITE_TRANSIENT);
								isExists = SQLITE_ROW == sqlite3_step(stmt);
							}
							sqlite3_finalize(stmt);

							if (isExists) {
								if (strategy == 1) {
									_sntprintf(res16, len, TEXT("%ls - skipped\n"), table16);
									_tcscat(result16, res16);
									continue;
								}

								char query8[strlen(table8) + 255];
								sprintf(query8, strategy == 3 ? "drop table if exists \"%s\".\"%s\"" : "delete from \"%s\".\"%s\"", schema8, table8);
								sqlite3_exec(db, query8, NULL, NULL, NULL);
							}
						}

						bool isError = false;
						if (strategy && isExport) {
							bool isExists = false;
							sqlite3_stmt* stmt;
							if (SQLITE_OK == sqlite3_prepare_v2(db,
									"with t(res) as (select odbc_query(?1, 'select * from \"'|| ?2 || '\" where 1=2')) " \
									"select 1 from t where coalesce(json_extract(res, '$.error'),'') = ''", -1, &stmt, 0)) {
								sqlite3_bind_text(stmt, 1, connectionString8, strlen(connectionString8), SQLITE_TRANSIENT);
								sqlite3_bind_text(stmt, 2, table8, strlen(table8), SQLITE_TRANSIENT);

								isExists = SQLITE_ROW == sqlite3_step(stmt);
							}
							sqlite3_finalize(stmt);

							if (isExists) {
								if (strategy == 1) {
									_sntprintf(res16, len, TEXT("%ls - skipped\n"), table16);
									_tcscat(result16, res16);
									continue;
								}

								sqlite3_stmt* stmt;
								if (SQLITE_OK == sqlite3_prepare_v2(db,
									"select json_extract(odbc_query(?1, printf('%s \"%s\"', ?3, ?2)), '$.error')", -1, &stmt, 0)) {
									sqlite3_bind_text(stmt, 1, connectionString8, strlen(connectionString8), SQLITE_TRANSIENT);
									sqlite3_bind_text(stmt, 2, table8, strlen(table8), SQLITE_TRANSIENT);
									sqlite3_bind_text(stmt, 3, strategy == 3 ?  "drop table " : "delete from ", 12, SQLITE_TRANSIENT);

									if (SQLITE_ROW == sqlite3_step(stmt)) {
										const char* err8 = (const char*)sqlite3_column_text(stmt, 0);
										if (err8 && strlen(err8)) {
											TCHAR res16[1024];
											TCHAR* err16 = utils::utf8to16(err8);
											_sntprintf(res16, 1023, TEXT("Couldn't %ls table %ls. Perhaps the driver doesn't support this operation.\n%ls"), strategy == 3 ? TEXT("drop") : TEXT("clear"), table16, err16);
											MessageBox(hWnd, res16, TEXT("Error"), MB_OK);
											delete [] err16;
											isError = true;
										}
									} else {
										showDbError(hWnd);
									}
								} else {
									showDbError(hWnd);
								}
								sqlite3_finalize(stmt);
							}
						}

						if (!isError) {
							sqlite3_stmt *stmt;
							rc = SQLITE_OK == sqlite3_prepare_v2(db,
								isExport ?
									"with t(res) as (select odbc_write(?2, ?1, ?3)) select coalesce(json_extract(res, '$.error'), printf('read: %i, inserted: %i', json_extract(res, '$.read'), json_extract(res, '$.inserted')) ) from t" :
									"with t(res) as (select odbc_read(?1, ?2, ?3)) select coalesce(json_extract(res, '$.error'), printf('read: %i, inserted: %i', json_extract(res, '$.read'), json_extract(res, '$.inserted')) ) from t",
								-1, &stmt, 0);

							if (rc) {
								sqlite3_bind_text(stmt, 1, connectionString8, strlen(connectionString8), SQLITE_TRANSIENT);

								char query8[strlen(schema8) + strlen(table8) + 64];;
								if (isExport)
									sprintf(query8, "select * from \"%s\".\"%s\"", schema8, table8);
								else
									sprintf(query8, "select * from \"%s\"", table8);
								sqlite3_bind_text(stmt, 2, query8, strlen(query8), SQLITE_TRANSIENT);

								char target8[strlen(schema8) + strlen(table8) + 64];
								if (isExport)
									sprintf(target8, "\"%s\"", table8);
								else
									sprintf(target8, "\"%s\".\"%s\"", schema8, table8);
								sqlite3_bind_text(stmt, 3, target8, strlen(target8), SQLITE_TRANSIENT);

								rc = SQLITE_ROW == sqlite3_step(stmt);
							}

							TCHAR* _res16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
							_sntprintf(res16, len, TEXT("%ls - %ls\n"), table16, _res16);
							_tcscat(result16, res16);
							delete [] _res16;

							sqlite3_finalize(stmt);
						} else {
							_sntprintf(res16, len, TEXT("%ls - error\n"), table16);
							_tcscat(result16, res16);
						}

						delete [] table8;
						delete [] schema8;
					}

					delete [] connectionString8;

					if (rc) {
						prefs::set("odbc-strategy", strategy);
						MessageBox(hWnd, result16, isExport ? TEXT("Export result") : TEXT("Import result"), MB_OK);
					} else {
						showDbError(hWnd);
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WMU_TARGET_CHANGED: {
				TCHAR schema16[255];
				GetDlgItemText(hWnd, IDC_DLG_ODBC_SCHEMA, schema16, 255);
				HWND hTablesWnd = GetDlgItem(hWnd, IDC_DLG_TABLES);

				sqlite3_stmt* stmt;
				char query8[1024];
				char* schema8 = utils::utf16to8(schema16);
				sprintf(query8, "select name from \"%s\".sqlite_master where type in ('table', 'view') order by type, name", schema8);
				delete [] schema8;

				if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
					ListView_SetData(hTablesWnd, stmt);
					ListView_SetColumnWidth(hTablesWnd, 1, 290);
				}

				sqlite3_finalize(stmt);
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgCompareDatabase (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hDbWnd = GetDlgItem(hWnd, IDC_DLG_DATABASE);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select path from recents order by time desc", -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt) && ComboBox_GetCount(hDbWnd) < 40) {
						TCHAR* db16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
						if (utils::isFileExists(db16))
							ComboBox_AddString(hDbWnd, db16);
						delete [] db16;
					}
				}
				sqlite3_finalize(stmt);

				SendMessage(GetDlgItem(hWnd, IDC_DLG_SCHEMA_DIFF), WM_SETFONT, (WPARAM)hFont, FALSE);
				SendMessage(GetDlgItem(hWnd, IDC_DLG_DATA_DIFF), WM_SETFONT, (WPARAM)hFont, FALSE);
				SendMessage(GetDlgItem(hWnd, IDC_DLG_DIFF_ROWS), WM_SETFONT, (WPARAM)hFont, FALSE);

				SetFocus(hDbWnd);
				utils::alignDialog(hWnd, hMainWnd);
			}
			break;

			case WM_SIZE: {
				int w = LOWORD(lParam);
				int h = HIWORD(lParam);

				POINTFLOAT s = utils::getDlgScale(hWnd);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_DATABASE), 0, 0, 0, w - 75 * s.x, 1, SWP_NOZORDER | SWP_NOMOVE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_DATABASE_SELECTOR), 0, w - 20 * s.x, 5 * s.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_COMPARE_SCHEMA), 0, w/2 - 100 * s.x - 2 * s.x, 25 * s.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_COMPARE_DATA), 0, w/2 + 2 * s.x, 25 * s.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_SCHEMA_DIFF), 0, 0, 0, w - 10 * s.x, h - 50 * s.y, SWP_NOZORDER | SWP_NOMOVE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_DATA_DIFF), 0, 0, 0, w - 10 * s.x, (h - 50 * s.y) * 0.4 - 5 * s.y, SWP_NOZORDER | SWP_NOMOVE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_DIFF_ROWS), 0, 5 * s.x, h - (h - 50 * s.y) * 0.6 - 5 * s.y, w - 10 * s.x, (h - 50 * s.y) * 0.6, SWP_NOZORDER);
				InvalidateRect(hWnd, NULL, true);
			}
			break;

			case WM_COMMAND: {
				TCHAR path16[MAX_PATH]{0};
				if (wParam == IDC_DLG_DATABASE_SELECTOR && utils::openFile(path16, TEXT("Databases (*.sqlite, *.sqlite3, *.db)\0*.sqlite;*.sqlite3;*.db\0All\0*.*\0"), hWnd))
					SetDlgItemText(hWnd, IDC_DLG_DATABASE, path16);

				if (wParam == IDC_DLG_COMPARE_SCHEMA || wParam == IDC_DLG_COMPARE_DATA) {
					GetDlgItemText(hWnd, IDC_DLG_DATABASE, path16, MAX_PATH);
					if(!utils::isFileExists(path16)) {
						MessageBox(hWnd, TEXT("Specify a compared database"), NULL, 0);
						return false;
					}

					sqlite3_exec(db, "detach database compared", NULL, NULL, NULL);
					char* path8 = utils::utf16to8(path16);
					if (!attachDb(&db, path8, "compared")) {
						MessageBox(hWnd, TEXT("Can't attach the compared database"), NULL, 0);
						return false;
					}

					BOOL isSchema = wParam == IDC_DLG_COMPARE_SCHEMA;

					ShowWindow(GetDlgItem(hWnd, IDC_DLG_SCHEMA_DIFF), isSchema ? SW_SHOW : SW_HIDE);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_DATA_DIFF), !isSchema ? SW_SHOW : SW_HIDE);
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_DIFF_ROWS), !isSchema ? SW_SHOW : SW_HIDE);

					ListView_Reset(GetDlgItem(hWnd, IDC_DLG_DIFF_ROWS));

					if (isSchema) {
						HWND hDiffWnd = GetDlgItem(hWnd, IDC_DLG_SCHEMA_DIFF);

						sqlite3_stmt *stmt;
						sqlite3_prepare_v2(db,
							"with t as (select type, name, sql from sqlite_master), " \
							"t2 as (select type, name, sql from compared.sqlite_master), " \
							"t3 as (select 1 src, * from t except select 1 src, * from t2), " \
							"t4 as (select 2 src, * from t2 except select 2 src, * from t), " \
							"t5 as (select * from t3 union  select * from t4),"
							"t6 as (select sum(src) idx, type, name from t5 group by type, name)"
							"select case when idx == 1 then '-->' when idx == 2 then '<--' else cast(x'e289a0' as text) end ' ', type, name from t6", -1, &stmt, 0);
						int rowCount = ListView_SetData(hDiffWnd, stmt);
						ListView_SetColumnWidth(hDiffWnd, 3, 495);
						if (!rowCount)
							MessageBox(hWnd, TEXT("Database schemas are the same"), TEXT("Info"), 0);
						sqlite3_finalize(stmt);
					} else {
						HWND hDiffWnd = GetDlgItem(hWnd, IDC_DLG_DATA_DIFF);
						sqlite3_exec(db, "drop table temp.compare_tables", NULL, NULL, NULL);

						sqlite3_stmt *stmt;
						if (SQLITE_OK == sqlite3_exec(db,
							"create table temp.compare_tables as " \
							"select sm.name, null cnt1, null cnt2, null diff from sqlite_master sm inner join compared.sqlite_master sm2 on " \
							"sm.type = sm2.type and sm.name = sm2.name and sm.type = 'table' where not exists( " \
							"select name from pragma_table_info(sm.name) except select name from pragma_table_info(sm.name) where schema = 'compared' " \
							"union all " \
							"select name from pragma_table_info(sm.name) where schema = 'compared' except select name from pragma_table_info(sm.name))",
							NULL, NULL, NULL) ||
							SQLITE_OK == sqlite3_exec(db,
							"create table temp.compare_tables as " \
							"select sm.name, null cnt1, null cnt2, null diff from pragma_table_list sm inner join pragma_table_list sm2 on " \
							"sm.schema = 'main' and sm2.schema = 'compared' and " \
							"sm.type = sm2.type and sm.name = sm2.name and sm.type = 'table' and sm.ncol = sm2.ncol",
							NULL, NULL, NULL)) {
								sqlite3_prepare_v2(db, "select name from temp.compare_tables", -1, &stmt, 0);
								while (SQLITE_ROW == sqlite3_step(stmt)) {
									const char* name = (const char*)sqlite3_column_text(stmt, 0);
									char query[1024 + 3 * strlen(name)];
									sprintf(query, "update temp.compare_tables set cnt1 = (select count(1) from \"%s\"), cnt2 = (select count(1) from compared.\"%s\") where name = \"%s\"", name, name, name);
									sqlite3_exec(db, query, NULL, NULL, NULL);
								}
								sqlite3_finalize(stmt);

								sqlite3_prepare_v2(db, "select name from temp.compare_tables where cnt1 = cnt2", -1, &stmt, 0);
								while (SQLITE_ROW == sqlite3_step(stmt)) {
									const char* name = (const char*)sqlite3_column_text(stmt, 0);
									char query[1024 + 3 * strlen(name)];
									sprintf(query,
										"with t as (select * from \"%s\" except select * from compared.\"%s\" union all select * from compared.\"%s\" except select * from \"%s\") "\
										"update temp.compare_tables set diff = (select count(1) from t) where name = \"%s\"", name, name, name, name, name);
									sqlite3_exec(db, query, NULL, NULL, NULL);
								}
								sqlite3_finalize(stmt);

								sqlite3_prepare_v2(db,
									"select name, iif(cnt1 <> cnt2, 'Rows count: ' || cnt1 || ' vs ' || cnt2, 'Different rows: ' || diff || ' of ' || cnt1) 'Reason' " \
									"from temp.compare_tables where cnt1 <> cnt2 or coalesce(diff, 0) > 0", -1, &stmt, 0);
								int rowCount = ListView_SetData(hDiffWnd, stmt);
								if (!rowCount)
									MessageBox(hWnd, TEXT("No differences were found in tables with the same structure"), TEXT("Info"), 0);
								sqlite3_finalize(stmt);
						} else {
							showDbError(hWnd);
						}
					}
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (pHdr->idFrom == IDC_DLG_SCHEMA_DIFF && pHdr->code == (DWORD)NM_DBLCLK) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					TCHAR type16[32], name16[1024];
					ListView_GetItemText(pHdr->hwndFrom, ia->iItem, 2, type16, 32);
					ListView_GetItemText(pHdr->hwndFrom, ia->iItem, 3, name16, 32);

					sqlite3_stmt *stmt;
					if (SQLITE_OK == sqlite3_prepare_v2(db,
						"select (select max(sql) from sqlite_master where type = ?1 and name = ?2)," \
						"(select max(sql) from compared.sqlite_master where type = ?1 and name = ?2)", -1, &stmt, 0)) {
						char* type8 = utils::utf16to8(type16);
						char* name8 = utils::utf16to8(name16);
						sqlite3_bind_text(stmt, 1, type8, strlen(type8), SQLITE_TRANSIENT);
						sqlite3_bind_text(stmt, 2, name8, strlen(name8), SQLITE_TRANSIENT);
						delete [] type8;
						delete [] name8;

						if (SQLITE_ROW == sqlite3_step(stmt)) {
							TDlgParam dp = {
								utils::utf8to16((const char*)sqlite3_column_text(stmt, 0)),
								utils::utf8to16((const char*)sqlite3_column_text(stmt, 1)),
								TEXT("Current"),
								TEXT("Compared"),
							};
							DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TEXT_COMPARISON), hWnd, (DLGPROC)cbDlgTextComparison, (LPARAM)&dp);

							delete [] dp.s1;
							delete [] dp.s2;
						}
					}
					sqlite3_finalize(stmt);
				}

				if (pHdr->idFrom == IDC_DLG_DATA_DIFF && pHdr->code == (DWORD)LVN_ITEMCHANGED) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					TCHAR name16[1024];
					ListView_GetItemText(pHdr->hwndFrom, ia->iItem, 1, name16, 32);

					char* name8 = utils::utf16to8(name16);
					char query8[1024 + 4 * strlen(name8)];
					sprintf(query8,
						"with t as (select '->' ' ', * from \"%s\" except select '->' ' ', * from compared.\"%s\"), " \
						"t2 as (select '<-' ' ', * from compared.\"%s\" except select '<-' ' ', * from \"%s\") " \
						"select * from t union all select * from t2",
						name8, name8, name8, name8);
					delete [] name8;

					HWND hRowsWnd = GetDlgItem(hWnd, IDC_DLG_DIFF_ROWS);
					ListView_Reset(hRowsWnd);

					sqlite3_stmt *stmt;
					sqlite3_prepare_v2(db, query8, -1, &stmt, 0);
					ListView_SetData(hRowsWnd, stmt);
					sqlite3_finalize(stmt);
				}
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE:
				sqlite3_exec(db, "detach database compared", NULL, NULL, NULL);
				EndDialog(hWnd, DLG_CANCEL);
				break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgDatabaseSearch (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				SetDlgItemText(hWnd, IDC_DLG_TABLENAMES, TEXT("All"));
				HWND hPatternWnd = GetDlgItem(hWnd, IDC_DLG_PATTERN);
				ComboBox_AddString(hPatternWnd, TEXT("Left and right wildcard"));
				ComboBox_AddString(hPatternWnd, TEXT("Exact match"));
				ComboBox_AddString(hPatternWnd, TEXT("Left wildcard"));
				ComboBox_AddString(hPatternWnd, TEXT("Right wildcard"));
				ComboBox_SetCurSel(hPatternWnd, 0);

				HWND hColTypeWnd = GetDlgItem(hWnd, IDC_DLG_COLTYPE);
				ComboBox_AddString(hColTypeWnd, TEXT("Any"));
				ComboBox_AddString(hColTypeWnd, TEXT("Number"));
				ComboBox_AddString(hColTypeWnd, TEXT("Text"));
				ComboBox_AddString(hColTypeWnd, TEXT("BLOB"));
				ComboBox_SetCurSel(hColTypeWnd, 0);

				HWND hNamesWnd = GetDlgItem(hWnd, IDC_DLG_TABLENAMES);
				LONG style = GetWindowLongPtr(hNamesWnd, GWL_EXSTYLE);
				style &= ~WS_EX_NOPARENTNOTIFY;
				SetWindowLongPtr(hNamesWnd, GWL_EXSTYLE, style);

				HWND hTablesWnd = GetDlgItem(hWnd, IDC_DLG_TABLES);
				ListView_SetExtendedListViewStyle(hTablesWnd, LVS_EX_CHECKBOXES);
				SetWindowPos(hTablesWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

				sqlite3_stmt *stmt;
				BOOL rc = SQLITE_OK == sqlite3_prepare_v2(db,
					"select * from (select name from sqlite_master where type = 'table' union all select 'sqlite_master') order by 1 desc",
					-1, &stmt, 0);

				while (rc && SQLITE_ROW == sqlite3_step(stmt)) {
					TCHAR* name16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
					LVITEM lvi;
					lvi.mask = LVIF_TEXT;
					lvi.iSubItem = 0;
					lvi.pszText = name16;
					lvi.cchTextMax = _tcslen(name16) + 1;
					ListView_InsertItem(hTablesWnd, &lvi);
					delete [] name16;
				}
				sqlite3_finalize(stmt);

				SendMessage(GetDlgItem(hWnd, IDC_DLG_SEARCH_RESULT), WM_SETFONT, (WPARAM)hFont, FALSE);
				SendMessage(GetDlgItem(hWnd, IDC_DLG_SEARCH_ROWS), WM_SETFONT, (WPARAM)hFont, FALSE);
				SendMessage(hWnd, DM_SETDEFID, IDC_DLG_SEARCH, 0);

				EnumChildWindows(hWnd, (WNDENUMPROC)cbEnumFixEditHeights, (LPARAM)utils::getEditHeight(hWnd));
				utils::alignDialog(hWnd, hMainWnd);
			}
			break;

			case WM_LBUTTONDOWN: {
				ShowWindow(GetDlgItem(hWnd, IDC_DLG_TABLES), SW_HIDE);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_SEARCH) {
					HWND hSearchWnd = GetDlgItem(hWnd, IDC_DLG_SEARCH_TEXT);
					int len = GetWindowTextLength(hSearchWnd);
					if (!len)
						return 0;

					TCHAR text16[len + 1]{0};
					GetWindowText(hSearchWnd, text16, len + 1);

					HWND hTablesWnd = GetDlgItem(hWnd, IDC_DLG_TABLES);
					BOOL isAll = TRUE;
					for (int i = 0; isAll && i < ListView_GetItemCount(hTablesWnd); i++)
						isAll = isAll && (ListView_GetCheckState(hTablesWnd, i) == BST_UNCHECKED);

					TCHAR names[MAX_TEXT_LENGTH]{0};
					for (int i = 0; i < ListView_GetItemCount(hTablesWnd); i++) {
						if (isAll || (ListView_GetCheckState(hTablesWnd, i) == BST_CHECKED)) {
							TCHAR name[256];
							ListView_GetItemText(hTablesWnd, i, 0, name, 255);
							_tcscat(names, _tcslen(names) > 0 ? TEXT("\", \"") : TEXT("\""));
							_tcscat(names, name);
						}
					}
					_tcscat(names, TEXT("\""));
					char* names8 = utils::utf16to8(names);
					int matchType = ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_PATTERN));
					int colType = ComboBox_GetCurSel(GetDlgItem(hWnd, IDC_DLG_COLTYPE));

					char query8[MAX_TEXT_LENGTH]{0};
					sprintf(query8, "select 'insert into temp.search_result (name, cnt, whr) select \"' || t.name || '\", count(1), " \
						" ''select * from \"'|| t.name || '\" where ' || group_concat('\"' || c.name || '\" like (\"%s\" || ?1 || \"%s\")', ' or ') || ''' " \
						"from \"' || t.name || '\" where ' || group_concat('\"' || c.name || '\" like (\"%s\" || ?1 || \"%s\")', ' or ')" \
						"from (select name from sqlite_master where sql is not null and type = 'table' union all select 'sqlite_master') t " \
						"left join pragma_table_info c on t.name = c.arg and c.schema = 'main' " \
						"where %s and t.name in (%s) group by t.name order by 1",
						matchType == 0 || matchType == 2 ? "%" : "",
						matchType == 0 || matchType == 3 ? "%" : "",
						matchType == 0 || matchType == 2 ? "%" : "",
						matchType == 0 || matchType == 3 ? "%" : "",
						colType == 1 ? "lower(c.type) in ('integer', 'real')" : colType == 2 ? "lower(c.type) = 'text'" : colType == 3 ? "lower(c.type) = 'blob'" : "1 = 1",
						names8);
					delete [] names8;

					sqlite3_exec(db, "drop table temp.search_result", NULL, 0, NULL);
					sqlite3_exec(db, "create table temp.search_result (name, cnt, whr)", NULL, 0, NULL);
					sqlite3_stmt *stmt;
					char* text8 = utils::utf16to8(text16);
					BOOL rc = SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0);
					while (rc && SQLITE_ROW == sqlite3_step(stmt)) {
						sqlite3_stmt *stmt2;
						if(SQLITE_OK == sqlite3_prepare_v2(db, (const char*)sqlite3_column_text(stmt, 0), -1, &stmt2, 0)) {
							sqlite3_bind_text(stmt2, 1, text8, strlen(text8), SQLITE_TRANSIENT);
							sqlite3_step(stmt2);
						}
						sqlite3_finalize(stmt2);
					}
					sqlite3_finalize(stmt);
					delete [] text8;

					HWND hResultWnd = GetDlgItem(hWnd, IDC_DLG_SEARCH_RESULT);
					if (SQLITE_OK == sqlite3_prepare_v2(db, "select name 'Table', cnt 'Rows found', whr 'Query' from temp.search_result where cnt > 0", -1, &stmt, 0)) {
						ListView_DeleteAllItems(hResultWnd);
						ListView_Reset(hResultWnd);
						ListView_SetData(hResultWnd, stmt);
						ListView_SetColumnWidth(hResultWnd, 2, 300);
						ListView_SetColumnWidth(hResultWnd, 3, 0);
						ListView_Reset(GetDlgItem(hWnd, IDC_DLG_SEARCH_ROWS));
					}
					sqlite3_finalize(stmt);

					SetDlgItemText(hWnd, IDC_DLG_SEARCH_QUERY_TEXT, text16);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					SendMessage(hWnd, WM_CLOSE, 0, 0);

				if (LOWORD(wParam) == IDC_DLG_TABLENAMES && (HIWORD(wParam) == (UINT)EN_KILLFOCUS) && (GetFocus() != GetDlgItem(hWnd, IDC_DLG_TABLES)))
					ShowWindow(GetDlgItem(hWnd, IDC_DLG_TABLES), SW_HIDE);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;

				if (pHdr->idFrom == IDC_DLG_TABLES && pHdr->code == LVN_ITEMCHANGED) {
					HWND hTablesWnd = pHdr->hwndFrom;
					TCHAR names[MAX_TEXT_LENGTH]{0};
					for (int i = 0; i < ListView_GetItemCount(hTablesWnd); i++) {
						if (ListView_GetCheckState(hTablesWnd, i) == BST_CHECKED) {
							TCHAR name[256];
							ListView_GetItemText(hTablesWnd, i, 0, name, 255);
							if (_tcslen(names) > 0)
								_tcscat(names, TEXT(", "));
							_tcscat(names, name);
						}
					}

					SetDlgItemText(hWnd, IDC_DLG_TABLENAMES, _tcslen(names) > 0 ? names : TEXT("All"));
				}

				if (pHdr->idFrom == IDC_DLG_TABLES && (pHdr->code == (UINT)NM_KILLFOCUS) && (GetFocus() != GetDlgItem(hWnd, IDC_DLG_TABLENAMES)))
					ShowWindow(pHdr->hwndFrom, SW_HIDE);

				if (pHdr->idFrom == IDC_DLG_SEARCH_RESULT && pHdr->code == (DWORD)LVN_KEYDOWN) {
					NMLVKEYDOWN* kd = (LPNMLVKEYDOWN) lParam;
					if (kd->wVKey == 0x43 && GetKeyState(VK_CONTROL)) { // Ctrl + C
						HWND hResultWnd = pHdr->hwndFrom;
						int pos = ListView_GetNextItem(hResultWnd, -1, LVNI_SELECTED);
						if (pos == -1)
							return 0;

						TCHAR text16[1024];
						GetDlgItemText(hWnd, IDC_DLG_SEARCH_QUERY_TEXT, text16, 1024);

						TCHAR query16[MAX_TEXT_LENGTH]{0};
						ListView_GetItemText(hResultWnd, pos, 3, query16, MAX_TEXT_LENGTH);

						TCHAR* res16 = utils::replaceAll(query16, TEXT("\" || ?1 || \""), text16);
						utils::setClipboardText(res16);
						delete [] res16;
					}
				}

				if (pHdr->idFrom == IDC_DLG_SEARCH_RESULT && pHdr->code == (DWORD)NM_CLICK) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					TCHAR text16[1024];
					GetDlgItemText(hWnd, IDC_DLG_SEARCH_QUERY_TEXT, text16, 1024);

					TCHAR query16[MAX_TEXT_LENGTH]{0};
					ListView_GetItemText(pHdr->hwndFrom, ia->iItem, 3, query16, MAX_TEXT_LENGTH);

					char* text8 = utils::utf16to8(text16);
					char* query8 = utils::utf16to8(query16);
					sqlite3_stmt *stmt;

					if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
						sqlite3_bind_text(stmt, 1, text8, strlen(text8), SQLITE_TRANSIENT);
						HWND hRowsWnd = GetDlgItem(hWnd, IDC_DLG_SEARCH_ROWS);
						ListView_Reset(hRowsWnd);
						ListView_SetData(hRowsWnd, stmt);
					}
					sqlite3_finalize(stmt);

					delete [] text8;
					delete [] query8;

				}
			}
			break;

			case WM_PARENTNOTIFY: {
				if (LOWORD(wParam) == WM_LBUTTONDOWN) {
					HWND hTablesWnd = GetDlgItem(hWnd, IDC_DLG_TABLES);
					ShowWindow(hTablesWnd, SW_SHOW);
				}
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}


	const TCHAR* GENERATOR_TYPE[MAX_ENTITY_COUNT] = {0};
	const TCHAR* SOURCES[MAX_ENTITY_COUNT] = {0};

	bool execute(const char* query8) {
		int rc = sqlite3_exec(db, query8, NULL, 0, NULL);
		bool res = rc == SQLITE_OK || rc == SQLITE_DONE;
		if (!res)
			printf("\nQuery: %s\nError: %s\n", query8, sqlite3_errmsg(db));

		return res;
	}

	int getDlgItemTextAsNumber(HWND hWnd, int id) {
		TCHAR buf16[32]{0};
		GetDlgItemText(hWnd, id, buf16, 31);
		return _ttoi(buf16);
	}

	LRESULT CALLBACK cbNewDataGeneratorType(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_COMMAND && HIWORD(wParam) == CBN_SELCHANGE)
			SendMessage(GetAncestor(hWnd, GA_ROOT), WMU_TYPE_CHANGED, (WPARAM)GetParent(hWnd), (LPARAM)hWnd);

		return CallWindowProc((WNDPROC)GetProp(hWnd, TEXT("WNDPROC")), hWnd, msg, wParam, lParam);
	}

	LRESULT CALLBACK cbNewDataGeneratorRefTable(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_COMMAND && HIWORD(wParam) == CBN_SELCHANGE)
			SendMessage(GetAncestor(hWnd, GA_ROOT), WMU_REFTABLE_CHANGED, (WPARAM)GetParent(hWnd), (LPARAM)hWnd);

		return CallWindowProc((WNDPROC)GetProp(hWnd, TEXT("WNDPROC")), hWnd, msg, wParam, lParam);
	}

	LRESULT CALLBACK cbNewDataGeneratorOneOf(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_COMMAND && HIWORD(wParam) == CBN_SELCHANGE)
			SendMessage(GetAncestor(hWnd, GA_ROOT), WMU_DATASET_CHANGED, (WPARAM)GetParent(hWnd), (LPARAM)hWnd);

		return CallWindowProc((WNDPROC)GetProp(hWnd, TEXT("WNDPROC")), hWnd, msg, wParam, lParam);
	}


	// USERDATA = lParam = buffer with the set name
	BOOL CALLBACK cbDlgDataGeneratorSet (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				SetWindowLongPtr(hWnd, GWLP_USERDATA, lParam);
                setEditorFont(GetDlgItem(hWnd, IDC_DLG_DATASET));
				EnumChildWindows(hWnd, (WNDENUMPROC)cbEnumFixEditHeights, (LPARAM)utils::getEditHeight(hWnd));
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK) {
					TCHAR* name16 = (TCHAR*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
					GetDlgItemText(hWnd, IDC_DLG_DATASET_NAME, name16, 2047);

					HWND hDatasetWnd = GetDlgItem(hWnd, IDC_DLG_DATASET);
					int len = GetWindowTextLength(hDatasetWnd);
					TCHAR dataset16[len + 1]{0};
					GetWindowText(hDatasetWnd, dataset16, len + 1);

					if (_tcslen(name16) == 0 || _tcslen(dataset16) == 0)
						return MessageBox(hWnd, TEXT("Both fields are mandatory"), 0, MB_OK);

					sqlite3_stmt* stmt;
					sqlite3_stmt* stmt2;
					BOOL rc = SQLITE_OK == sqlite3_prepare_v2(prefs::db, "insert into generators (type, value) values (?1, ?2)", -1, &stmt, 0);
					BOOL rc2 = SQLITE_OK == sqlite3_prepare_v2(db, "insert into temp.generators (type, value) values (?1, ?2)", -1, &stmt2, 0);
					if (rc && rc2) {
						sqlite3_exec(prefs::db, "begin", 0, 0, 0);
						sqlite3_exec(db, "begin", 0, 0, 0);

						char* name8 = utils::utf16to8(name16);

						TCHAR* value16 = _tcstok (dataset16, TEXT(";"));
						while (value16 != NULL) {
							char* value8 = utils::utf16to8(value16);
							sqlite3_bind_text(stmt, 1, name8, -1, SQLITE_TRANSIENT);
							sqlite3_bind_text(stmt, 2, value8, -1, SQLITE_TRANSIENT);
							sqlite3_bind_text(stmt2, 1, name8, -1, SQLITE_TRANSIENT);
							sqlite3_bind_text(stmt2, 2, value8, -1, SQLITE_TRANSIENT);
							delete [] value8;
							sqlite3_step(stmt);
							sqlite3_step(stmt2);
							sqlite3_reset(stmt);
							sqlite3_reset(stmt2);

							value16 = _tcstok (NULL, TEXT(";"));
						}

						delete [] name8;

						sqlite3_exec(prefs::db, "commit", 0, 0, 0);
						sqlite3_exec(db, "commit", 0, 0, 0);
					}
					sqlite3_finalize(stmt);
					sqlite3_finalize(stmt2);

					EndDialog(hWnd, DLG_OK);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL) {
					EndDialog(hWnd, DLG_CANCEL);
				}
			}
			break;
		}

		return false;
	}

	// USERDATA = 1 if a last generation is ok
	// lParam is a target table (optional)
	BOOL CALLBACK cbDlgDataGenerator (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hTable = GetDlgItem(hWnd, IDC_DLG_TABLENAME);

				int rowCount = prefs::get("data-generator-row-count");
				TCHAR rowCount16[32]{0};
				_itot(rowCount, rowCount16, 10);
				SetDlgItemText(hWnd, IDC_DLG_GEN_ROW_COUNT, rowCount16);

				if (prefs::get("data-generator-truncate"))
					Button_SetCheck(GetDlgItem(hWnd, IDC_DLG_GEN_ISTRUNCATE), BST_CHECKED);

				if (lParam)	{
					ComboBox_AddString(hTable, (TCHAR*)lParam);
					EnableWindow(hTable, !lParam);
				} else {
					sqlite3_stmt *stmt, *stmt2;
					if (SQLITE_OK == sqlite3_prepare_v2(db,
						"with t as (select name from pragma_database_list() where name <> 'temp') " \
						"select name from t order by iif(name = 'main', 1, name)", -1, &stmt, 0)) {
						while (SQLITE_ROW == sqlite3_step(stmt)) {
							char* schema8 = (char *)sqlite3_column_text(stmt, 0);
							char query8[strlen(schema8) + 1024];
							sprintf(query8,
								"select iif('%s' = 'main', name, '%s' || '.' || name) from \"%s\".sqlite_master where type = 'table' and name <> 'sqlite_sequence' order by 1",
								schema8, schema8, schema8);
							if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt2, 0)) {
								while (SQLITE_ROW == sqlite3_step(stmt2)) {
									TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt2, 0));
									ComboBox_AddString(hTable, name16);
									delete [] name16;
								}
							}
							sqlite3_finalize(stmt2);
						}
					}
					sqlite3_finalize(stmt);
				}
				ComboBox_SetCurSel(hTable, 0);

				if (!GENERATOR_TYPE[0]) {
					int i = 0;
					sqlite3_stmt *stmt;
					while (GENERATOR_TYPE[i] && i < MAX_ENTITY_COUNT) {
						delete [] GENERATOR_TYPE[i];
						GENERATOR_TYPE[i] = 0;
						i++;
					}

					if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select distinct type from generators order by 1", -1, &stmt, 0)) {
						while (SQLITE_ROW == sqlite3_step(stmt)) {
							GENERATOR_TYPE[i] = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
							i++;
						}
					}
					sqlite3_finalize(stmt);

					i = 0;
					while (SOURCES[i] && i < MAX_ENTITY_COUNT) {
						delete [] SOURCES[i];
						SOURCES[i] = 0;
						i++;
					}

					if (SQLITE_OK == sqlite3_prepare_v2(db, "select name from sqlite_master where type in ('table', 'view') and name <> 'sqlite_sequence' order by 1", -1, &stmt, 0)) {
						int i = 0;
						while (SQLITE_ROW == sqlite3_step(stmt)) {
							SOURCES[i] = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
							i++;
						}
					}
					sqlite3_finalize(stmt);

					// Copy generators table
					sqlite3_exec(db, "drop table temp.generators", 0, 0, 0);
					sqlite3_exec(db, "create table temp.generators (type, value)", 0, 0, 0);
					sqlite3_prepare_v2(prefs::db, "select type, value from generators order by 1", -1, &stmt, 0);

					sqlite3_stmt *istmt;
					sqlite3_prepare_v2(db, "insert into temp.generators (type, value) values (?1, ?2)", -1, &istmt, 0);
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						const char*	key = (const char*)sqlite3_column_text(stmt, 0);
						const char* value = (const char*)sqlite3_column_text(stmt, 1);
						sqlite3_bind_text(istmt, 1, key, strlen(key), SQLITE_TRANSIENT);
						sqlite3_bind_text(istmt, 2, value, strlen(value), SQLITE_TRANSIENT);
						sqlite3_step(istmt);
						sqlite3_reset(istmt);
					}
					sqlite3_finalize(stmt);
					sqlite3_finalize(istmt);
				}

				HWND hColumnsWnd = GetDlgItem(hWnd, IDC_DLG_GEN_COLUMNS);
				SetProp(hColumnsWnd, TEXT("WNDPROC"), (HANDLE)SetWindowLongPtr(hColumnsWnd, GWLP_WNDPROC, (LONG_PTR)dialogs::cbNewScroll));
				PostMessage(hWnd, WMU_TARGET_CHANGED, 0, 0);

				EnumChildWindows(hWnd, (WNDENUMPROC)cbEnumFixEditHeights, (LPARAM)utils::getEditHeight(hWnd));
				utils::alignDialog(hWnd, hMainWnd);
				SetFocus(hTable);
			}
			break;

			case WMU_TARGET_CHANGED: {
				HWND hColumnsWnd = GetDlgItem(hWnd, IDC_DLG_GEN_COLUMNS);
				EnumChildWindows(hColumnsWnd, (WNDENUMPROC)cbEnumChildren, (LPARAM)ACTION_DESTROY);

				TCHAR name16[1024]{0};
				GetDlgItemText(hWnd, IDC_DLG_TABLENAME, name16, 1024);
				TCHAR* schema16 = utils::getTableName(name16, true);
				TCHAR* tablename16 = utils::getTableName(name16);

				TCHAR query16[MAX_TEXT_LENGTH + 1]{0};
				_sntprintf(query16, MAX_TEXT_LENGTH, TEXT("select name from pragma_table_info(\"%ls\") where schema = \"%ls\" order by cid"), tablename16, schema16);
				delete [] tablename16;
				delete [] schema16;

				POINTFLOAT s = utils::getDlgScale(hWnd);
				RECT rc;
				GetClientRect(GetDlgItem(hWnd, IDC_DLG_TABLENAME), &rc);
				int comboH = rc.bottom;
				int lineH = comboH + 2 * s.y;

				SendMessage(hColumnsWnd, WM_SETREDRAW, FALSE, 0);
				char* query8 = utils::utf16to8(query16);
				sqlite3_stmt *stmt;
				int rowNo = 0;
				if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						HWND hColumnWnd = CreateWindow(WC_STATIC, NULL, WS_VISIBLE | WS_CHILD, 0, lineH * rowNo, 470 * s.x, lineH, hColumnsWnd, (HMENU)IDC_DLG_GEN_COLUMN, GetModuleHandle(0), 0);

						TCHAR* colname16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						CreateWindow(WC_STATIC, colname16, WS_VISIBLE | WS_CHILD | SS_SIMPLE, 0, 2 * s.y, 65 * s.x, comboH, hColumnWnd, (HMENU)IDC_DLG_GEN_COLUMN_NAME, GetModuleHandle(0), 0);
						delete [] colname16;

						HWND hTypeWnd = CreateWindow(WC_COMBOBOX, NULL, WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 65 * s.x, 0, 65 * s.x, 100 * s.y, hColumnWnd, (HMENU)IDC_DLG_GEN_COLUMN_TYPE, GetModuleHandle(0), 0);
						ComboBox_AddString(hTypeWnd, TEXT("none"));
						ComboBox_AddString(hTypeWnd, TEXT("sequence"));
						ComboBox_AddString(hTypeWnd, TEXT("number"));
						ComboBox_AddString(hTypeWnd, TEXT("date"));
						ComboBox_AddString(hTypeWnd, TEXT("reference to"));
						ComboBox_AddString(hTypeWnd, TEXT("expression"));
						ComboBox_AddString(hTypeWnd, TEXT("one of"));

						ComboBox_SetCurSel(hTypeWnd, 0);
						SetProp(hTypeWnd, TEXT("WNDPROC"), (HANDLE)SetWindowLongPtr(hTypeWnd, GWLP_WNDPROC, (LONG_PTR)cbNewDataGeneratorType));

						CreateWindow(WC_STATIC, NULL, WS_VISIBLE | WS_CHILD | WS_TABSTOP, 135 * s.x, 0, 140 * s.x, lineH, hColumnWnd, (HMENU)IDC_DLG_GEN_OPTION, GetModuleHandle(0), 0);

						rowNo++;
					}
				}
				sqlite3_finalize(stmt);
				delete [] query8;
				SendMessage(hColumnsWnd, WM_SETREDRAW, TRUE, 0);

				EnumChildWindows(hColumnsWnd, (WNDENUMPROC)cbEnumChildren, (LPARAM)ACTION_SETPARENTFONT);
				SetProp(hColumnsWnd, TEXT("SCROLLY"), 0);
				SendMessage(hColumnsWnd, WMU_SET_SCROLL_HEIGHT, rowNo * lineH, 0);

				InvalidateRect(hColumnsWnd, NULL, TRUE);
			}
			break;

			case WMU_TYPE_CHANGED: {
				HWND hColumnWnd = (HWND)wParam;
				HWND hTypeWnd = (HWND)lParam;
				HWND hOptionWnd = GetDlgItem(hColumnWnd, IDC_DLG_GEN_OPTION);
				EnumChildWindows(hOptionWnd, (WNDENUMPROC)cbEnumChildren, (LPARAM)ACTION_DESTROY);

				POINTFLOAT s = utils::getDlgScale(hWnd);
				RECT rc;
				GetClientRect(GetDlgItem(hWnd, IDC_DLG_TABLENAME), &rc);
				int comboH = rc.bottom;

				TCHAR buf16[64];
				GetWindowText(hTypeWnd, buf16, 63);

				if (_tcscmp(buf16, TEXT("sequence")) == 0) {
					CreateWindow(WC_STATIC, TEXT("Start"), WS_VISIBLE | WS_CHILD, 0, 2 * s.y, 25 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_LABEL, GetModuleHandle(0), 0);
					CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT("1"), WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL | WS_TABSTOP, 25 * s.x, 0, 40 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_START, GetModuleHandle(0), 0);
				}

				if (_tcscmp(buf16, TEXT("number")) == 0) {
					CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT("1"), WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 40 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_START, GetModuleHandle(0), 0);
					CreateWindow(WC_STATIC, TEXT("-"), WS_VISIBLE | WS_CHILD | SS_CENTER, 40 * s.x, 2 * s.y, 10 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_LABEL, GetModuleHandle(0), 0);
					CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT("100"), WS_VISIBLE | WS_CHILD | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL | WS_TABSTOP, 50 * s.x, 0, 40 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_END, GetModuleHandle(0), 0);
					CreateWindow(WC_STATIC, TEXT("x"), WS_VISIBLE | WS_CHILD | SS_CENTER, 91 * s.x, 2 * s.y, 10 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_LABEL, GetModuleHandle(0), 0);
					CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, TEXT("100"), WS_VISIBLE | WS_CHILD | ES_CENTER | WS_TABSTOP, 100 * s.x, 0, 39 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_MULTIPLIER, GetModuleHandle(0), 0);
				}

				if (_tcscmp(buf16, TEXT("date")) == 0) {
					CreateWindowEx(0, DATETIMEPICK_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 65 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_START, GetModuleHandle(0), 0);
					CreateWindow(WC_STATIC, TEXT("-"), WS_VISIBLE | WS_CHILD | SS_CENTER, 65 * s.x, 2 * s.y, 9 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_LABEL, GetModuleHandle(0), 0);
					CreateWindowEx(0, DATETIMEPICK_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 74 * s.x, 0, 65 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_END, GetModuleHandle(0), 0);
				}

				if (_tcscmp(buf16, TEXT("reference to")) == 0) {
					HWND hRefTableWnd = CreateWindow(WC_COMBOBOX, NULL, WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0, 70 * s.x, 100 * s.y, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_TABLE, GetModuleHandle(0), 0);
					for (int i = 0; SOURCES[i]; i++)
						ComboBox_AddString(hRefTableWnd, SOURCES[i]);
					SetProp(hRefTableWnd, TEXT("WNDPROC"), (HANDLE)SetWindowLongPtr(hRefTableWnd, GWLP_WNDPROC, (LONG_PTR)cbNewDataGeneratorRefTable));

					CreateWindow(WC_COMBOBOX, NULL, WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 70 * s.x, 0, 69 * s.x, 100 * s.y, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_COLUMN, GetModuleHandle(0), 0);
					ComboBox_SetCurSel(hRefTableWnd, 0);
					SendMessage(hWnd, WMU_REFTABLE_CHANGED, (WPARAM)hOptionWnd, (LPARAM)hRefTableWnd);
				}

				if (_tcscmp(buf16, TEXT("expression")) == 0) {
					HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, NULL, WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_LEFT | WS_TABSTOP, 0, 0, 140 * s.x, comboH, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_EXPR, GetModuleHandle(0), 0);
					Edit_SetCueBannerText(hEdit, TEXT("e.g. id + length(id)"));
				}

				if (_tcscmp(buf16, TEXT("one of")) == 0) {
					HWND hOneOfWnd = CreateWindow(WC_COMBOBOX, NULL, WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0, 140 * s.x, 100 * s.y, hOptionWnd, (HMENU)IDC_DLG_GEN_OPTION_ONEOF, GetModuleHandle(0), 0);
					for (int i = 0; GENERATOR_TYPE[i]; i++)
						ComboBox_AddString(hOneOfWnd, GENERATOR_TYPE[i]);
					ComboBox_AddString(hOneOfWnd, TEXT("New set..."));
					ComboBox_SetCurSel(hOneOfWnd, 0);

					SetProp(hOneOfWnd, TEXT("WNDPROC"), (HANDLE)SetWindowLongPtr(hOneOfWnd, GWLP_WNDPROC, (LONG_PTR)cbNewDataGeneratorOneOf));
				}

				EnumChildWindows(hOptionWnd, (WNDENUMPROC)cbEnumChildren, (LPARAM)ACTION_SETPARENTFONT);
			}
			break;

			case WMU_REFTABLE_CHANGED: {
				HWND hOptionWnd = (HWND)wParam;
				HWND hRefTableWnd = (HWND)lParam;
				HWND hRefColumnWnd = GetDlgItem(hOptionWnd, IDC_DLG_GEN_OPTION_COLUMN);
				ComboBox_ResetContent(hRefColumnWnd);

				TCHAR buf16[255]{0};
				TCHAR query16[MAX_TEXT_LENGTH + 1]{0};
				GetWindowText(hRefTableWnd, buf16, 255);

				_sntprintf(query16, MAX_TEXT_LENGTH, TEXT("select name from pragma_table_info(\"%ls\") order by cid"), buf16);
				char* query8 = utils::utf16to8(query16);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* colname16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hRefColumnWnd, colname16);
						delete [] colname16;
					}
				}
				sqlite3_finalize(stmt);
				delete [] query8;

				ComboBox_SetCurSel(hRefColumnWnd, 0);
			}
			break;

			case WMU_DATASET_CHANGED: {
				HWND hOneOfWnd = (HWND)lParam;

				TCHAR setName16[2048]{0};
				GetWindowText(hOneOfWnd, setName16, 2047);
				if (_tcscmp(setName16, TEXT("New set...")))
					return false;

				if (DLG_OK == DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_GENERATE_DATA_SET), hWnd, (DLGPROC)cbDlgDataGeneratorSet, (LPARAM)setName16)) {
					int i = 0;
					for (i = 0; i < MAX_ENTITY_COUNT && GENERATOR_TYPE[i]; i++);
					if (i < MAX_ENTITY_COUNT) {
						TCHAR* type16 = new TCHAR[_tcslen(setName16) + 1]{0};
						_tcscpy(type16, setName16);
						GENERATOR_TYPE[i] = type16;
						ComboBox_InsertString(hOneOfWnd, i, type16);
						ComboBox_SetCurSel(hOneOfWnd, i);
					}
				}
			}
			break;

			case WM_COMMAND: {
				WORD id = LOWORD(wParam);
				WORD cmd = HIWORD(wParam);
				if (cmd == CBN_SELCHANGE && id == IDC_DLG_TABLENAME)
					SendMessage(hWnd, WMU_TARGET_CHANGED, 0, 0);

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL) {
					EndDialog(hWnd, GetWindowLongPtr(hWnd, GWLP_USERDATA) ? DLG_OK : DLG_CANCEL);
				}

				if (wParam == IDC_DLG_OK || wParam == IDOK)	{
					execute("drop table if exists temp.data_generator");

					bool isTruncate = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_GEN_ISTRUNCATE));
					if (isTruncate && MessageBox(hWnd, TEXT("All data from table will be erased. Continue?"), TEXT("Confirmation"), MB_OKCANCEL | MB_ICONASTERISK) != IDOK)
						return true;

					TCHAR name16[1024]{0};
					GetDlgItemText(hWnd, IDC_DLG_TABLENAME, name16, 1024);
					TCHAR* schema16 = utils::getTableName(name16, true);
					TCHAR* tablename16 = utils::getTableName(name16);

					char* schema8 = utils::utf16to8(schema16);
					char* tablename8 = utils::utf16to8(tablename16);
					delete [] schema16;
					delete [] tablename16;

					char query8[MAX_TEXT_LENGTH]{0};
					sprintf(query8, "create table temp.data_generator as select null rownum, t.* from \"%s\".\"%s\" t where 1 = 2", schema8, tablename8);
					execute(query8);

					int rowCount = getDlgItemTextAsNumber(hWnd, IDC_DLG_GEN_ROW_COUNT);

					sprintf(query8,
						"with recursive series(val) as (select 1 union all select val + 1 from series limit %i) " \
						"insert into temp.data_generator (rownum) select val from series", rowCount);
					execute(query8);

					char columns8[MAX_TEXT_LENGTH]{0};

					bool rc = true;
					for (int isExpr = 0; isExpr < 2; isExpr++) {
						HWND hColumnWnd = GetWindow(GetDlgItem(hWnd, IDC_DLG_GEN_COLUMNS), GW_CHILD);

						while(IsWindow(hColumnWnd) && rc){
							TCHAR name16[128]{0};
							GetDlgItemText(hColumnWnd, IDC_DLG_GEN_COLUMN_NAME, name16, 127);
							char* name8 = utils::utf16to8(name16);
							if (strlen(columns8) > 0)
								strcat(columns8, ", ");
							strcat(columns8, name8);
							delete [] name8;

							HWND hTypeWnd = GetDlgItem(hColumnWnd, IDC_DLG_GEN_COLUMN_TYPE);
							HWND hOptionWnd = GetDlgItem(hColumnWnd, IDC_DLG_GEN_OPTION);
							TCHAR type16[128]{0};
							GetWindowText(hTypeWnd, type16, 127);
							TCHAR query16[MAX_TEXT_LENGTH + 1]{0};

							if (!isExpr && _tcscmp(type16, TEXT("sequence")) == 0) {
								int start = getDlgItemTextAsNumber(hOptionWnd, IDC_DLG_GEN_OPTION_START);
								_sntprintf(query16, MAX_TEXT_LENGTH, TEXT("update temp.data_generator set \"%ls\" = rownum + %i - 1"), name16, start);
							}

							if (!isExpr && _tcscmp(type16, TEXT("number")) == 0) {
								int start = getDlgItemTextAsNumber(hOptionWnd, IDC_DLG_GEN_OPTION_START);
								int end = getDlgItemTextAsNumber(hOptionWnd, IDC_DLG_GEN_OPTION_END);
								TCHAR multi[32]{0};
								GetDlgItemText(hOptionWnd, IDC_DLG_GEN_OPTION_MULTIPLIER, multi, 31);
								TCHAR* multi2 = utils::replace(multi, TEXT(","), TEXT("."));

								_sntprintf(query16, MAX_TEXT_LENGTH, TEXT("update temp.data_generator set \"%ls\" = cast((%i + (%i - %i + 1) * (random()  / 18446744073709551616 + 0.5)) as integer) * %ls"), name16, start, end, start, utils::isNumber(multi2, NULL) ? multi2 : TEXT("0"));
								delete [] multi2;
							}

							if (!isExpr && _tcscmp(type16, TEXT("reference to")) == 0) {
								TCHAR reftable16[256]{0};
								GetDlgItemText(hOptionWnd, IDC_DLG_GEN_OPTION_TABLE, reftable16, 255);

								TCHAR refcolumn16[256]{0};
								GetDlgItemText(hOptionWnd, IDC_DLG_GEN_OPTION_COLUMN, refcolumn16, 255);

								_sntprintf(query16, MAX_TEXT_LENGTH, TEXT("with t as (select %ls value from \"%ls\" order by random()), " \
									"series(val) as (select 1 union all select val + 1 from series limit (select ceil(%i.0/count(1)) from t)), " \
									"t2 as (select t.value FROM t, series order by random()), " \
									"t3 as (select rownum(1) rownum, t2.value from t2 order by 1 limit %i)"
									"update temp.data_generator set \"%ls\" = t3.value from t3 where t3.rownum = temp.data_generator.rownum"),
									refcolumn16, reftable16, rowCount, rowCount, name16);
							}

							if (!isExpr && _tcscmp(type16, TEXT("date")) == 0) {
								SYSTEMTIME start = {0}, end = {0};
								DateTime_GetSystemtime(GetDlgItem(hOptionWnd, IDC_DLG_GEN_OPTION_START), &start);
								DateTime_GetSystemtime(GetDlgItem(hOptionWnd, IDC_DLG_GEN_OPTION_END), &end);

								TCHAR start16[32] = {0};
								_sntprintf(start16, 31, TEXT("%i-%0*i-%0*i"), start.wYear, 2, start.wMonth, 2, start.wDay);

								TCHAR end16[32] = {0};
								_sntprintf(end16, 31, TEXT("%i-%0*i-%0*i"), end.wYear, 2, end.wMonth, 2, end.wDay);

								_sntprintf(query16, MAX_TEXT_LENGTH,TEXT("update temp.data_generator set \"%ls\" = date('%ls', '+' || ((strftime('%%s', '%ls', '+1 day', '-1 second') - strftime('%%s', '%ls')) * (random()  / 18446744073709551616 + 0.5)) || ' second')"),
									name16, start16, end16, start16);
							}

							if (isExpr && _tcscmp(type16, TEXT("expression")) == 0) {
								HWND hExpressionWnd = GetDlgItem(hOptionWnd, IDC_DLG_GEN_OPTION_EXPR);
								int len = GetWindowTextLength(hExpressionWnd);
								TCHAR expr16[len + 1]{0};
								GetWindowText(hExpressionWnd, expr16, len + 1);

								_sntprintf(query16, MAX_TEXT_LENGTH, TEXT("update temp.data_generator set \"%ls\" = %ls"), name16, expr16);
							}

							if (!isExpr && _tcscmp(type16, TEXT("one of")) == 0) {
								HWND hOneOfWnd = GetDlgItem(hOptionWnd, IDC_DLG_GEN_OPTION_ONEOF);
								int len = GetWindowTextLength(hOneOfWnd);
								TCHAR setName16[len + 1]{0};
								GetWindowText(hOneOfWnd, setName16, len + 1);

								if (_tcscmp(setName16, TEXT("New set..."))) {
									_sntprintf(query16, MAX_TEXT_LENGTH, TEXT("with t as (select type, value from temp.generators where type = \"%ls\" order by random()), "\
										"series(val) as (select 1 union all select val + 1 from series limit (select ceil(%i.0/count(1)) from t)), " \
										"t2 as (select t.value FROM t, series order by random()), " \
										"t3 as (select rownum(1) rownum, t2.value from t2 order by 1 limit %i)" \
										"update temp.data_generator set \"%ls\" = (select value from t3 where t3.rownum = temp.data_generator.rownum)"),
										setName16, rowCount, rowCount, name16);
								}
							}

							char* query8 = utils::utf16to8(query16);
							rc = execute(query8);
							delete [] query8;

							hColumnWnd = GetWindow(hColumnWnd, GW_HWNDNEXT);
						}
					}

					if (!rc) {
						showDbError(hWnd);
						return 0;
					}

					prefs::set("data-generator-row-count", rowCount);
					prefs::set("data-generator-truncate", +isTruncate);

					if (isTruncate) {
						sprintf(query8, "delete from \"%s\".\"%s\"", schema8, tablename8);
						execute(query8);
					}

					snprintf(query8, MAX_TEXT_LENGTH, "insert into \"%s\".\"%s\" (%s) select %s from temp.data_generator", schema8, tablename8, columns8, columns8);
					rc = execute(query8);
					if (rc)
						MessageBox(hWnd, TEXT("Done!"), TEXT("Info"), MB_OK);
					else
						showDbError(hWnd);
					SetWindowLongPtr(hWnd, GWLP_USERDATA, rc);

					delete [] schema8;
					delete [] tablename8;
				}
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgStatistics (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				sqlite3_exec(db, "drop table temp.row_statistics;", NULL, 0, NULL);
				sqlite3_exec(db, "create table temp.row_statistics (name text, cnt integer);", NULL, 0, NULL);

				sqlite3_stmt* stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db,
						"select 'insert into temp.row_statistics (name, cnt) select ''' || name || ''', count(1) from \"' || name || '\"' from sqlite_master where type in ('table')",
						-1, &stmt, 0)) {

						while (SQLITE_ROW == sqlite3_step(stmt))
							sqlite3_exec(db, (const char*)sqlite3_column_text(stmt, 0), NULL, 0, NULL);
				}
				sqlite3_finalize(stmt);

				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_STATISTICS);
				SendMessage(hListWnd, WM_SETFONT, (WPARAM)hFont, FALSE);

				if (SQLITE_OK == sqlite3_prepare_v2(db,
					"select s.name, sm.type, SUM(payload) 'Payload size, B', tosize(SUM(pgsize)) 'Total size', r.cnt 'Rows' " \
					"from dbstat s left join sqlite_master sm on s.name = sm.name left join temp.row_statistics r on s.name = r.name group by s.name;",
					-1, &stmt, 0))
					ListView_SetData(hListWnd, stmt);
				sqlite3_finalize(stmt);

				float z = utils::getDlgScale(hWnd).x;
				HWND hStatusWnd = CreateStatusWindow(WS_CHILD | WS_VISIBLE, NULL, hWnd, IDC_DLG_STATUSBAR);
				int sizes[5] = {(int)(z * 60), (int)(z * 120), (int)(z * 180), (int)(z * 240), -1};
				SendMessage(hStatusWnd, SB_SETPARTS, 5, (LPARAM)&sizes);
				if (SQLITE_OK == sqlite3_prepare_v2(db,
					"select sum(type = 'table'), sum(type = 'view'), sum(type = 'index'), sum(type = 'trigger'), tosize(?1) from sqlite_master where lower(type) in ('table', 'view', 'trigger', 'index')",
					-1, &stmt, 0)) {

					struct _stat st;
					TCHAR* path16 = utils::utf8to16(sqlite3_db_filename(db, 0));
					sqlite3_bind_int64(stmt, 1, (_tstat(path16, &st) == 0) ? st.st_size : 0);
					delete [] path16;

					if (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR buf16[256];
						_sntprintf(buf16, 255, TEXT(" TABLES: %i"), sqlite3_column_int(stmt, 0));
						SendMessage(hStatusWnd, SB_SETTEXT, 0, (LPARAM)buf16);

						_sntprintf(buf16, 255, TEXT(" VIEWS: %i"), sqlite3_column_int(stmt, 1));
						SendMessage(hStatusWnd, SB_SETTEXT, 1, (LPARAM)buf16);

						_sntprintf(buf16, 255, TEXT(" INDEXES: %i"), sqlite3_column_int(stmt, 2));
						SendMessage(hStatusWnd, SB_SETTEXT, 2, (LPARAM)buf16);

						_sntprintf(buf16, 255, TEXT(" TRIGGERS: %i"), sqlite3_column_int(stmt, 3));
						SendMessage(hStatusWnd, SB_SETTEXT, 3, (LPARAM)buf16);

						TCHAR* size16 = utils::utf8to16((const char *)sqlite3_column_text(stmt, 4));
						_sntprintf(buf16, 255, TEXT(" FILE SIZE: %ls"), size16);
						SendMessage(hStatusWnd, SB_SETTEXT, 4, (LPARAM)buf16);
						delete [] size16;
					}
				}
				sqlite3_finalize(stmt);

				RECT rc, rc2;
				GetClientRect(hListWnd, &rc);

				HWND hHeader = ListView_GetHeader(hListWnd);
				int colCount = Header_GetItemCount(hHeader);
				Header_GetItemRect(hHeader, colCount - 1, &rc2);

				SetWindowPos(hWnd, 0, 0, 0, rc2.right + GetSystemMetrics(SM_CXVSCROLL) + 2 * GetSystemMetrics(SM_CXSIZEFRAME) + 2 * GetSystemMetrics(SM_CXEDGE), rc.bottom, SWP_NOZORDER | SWP_NOMOVE);
				utils::alignDialog(hWnd, hMainWnd, true);
				SetFocus(hListWnd);
			}
			break;

			case WM_SIZE: {
				HWND hStatusWnd = GetDlgItem(hWnd, IDC_DLG_STATUSBAR);
				SendMessage(hStatusWnd, WM_SIZE, 0, 0);

				RECT rc;
				GetWindowRect(hStatusWnd, &rc);
				POINT p{rc.right, rc.top};
				ScreenToClient(hWnd, &p);
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_STATISTICS);
				SetWindowPos(hListWnd, 0, 0, 0, p.x, p.y - 1, SWP_NOZORDER | SWP_NOMOVE);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (pHdr->code == LVN_COLUMNCLICK && pHdr->idFrom == IDC_DLG_STATISTICS) {
					NMLISTVIEW* pLV = (NMLISTVIEW*)lParam;
					return ListView_Sort(pHdr->hwndFrom, pLV->iSubItem);
				}

				if (pHdr->code == (DWORD)NM_DBLCLK && pHdr->idFrom == IDC_DLG_STATISTICS) {
					NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
					TCHAR text16[1024];
					GetDlgItemText(hWnd, IDC_DLG_STATISTICS, text16, 1024);

					TCHAR name16[256]{0}, type16[256]{0};
					ListView_GetItemText(pHdr->hwndFrom, ia->iItem, 1, name16, 255);
					ListView_GetItemText(pHdr->hwndFrom, ia->iItem, 2, type16, 255);
					if (_tcscmp(type16, TEXT("table")) == 0) {
						DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_EDITDATA), hWnd, (DLGPROC)&dialogs::cbDlgEditData, (LPARAM)name16);
						SetFocus(pHdr->hwndFrom);
					}
				}
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgForeignKeyCheck (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_FOREIGN_KEY_CHECK);
				SendMessage(hListWnd, WM_SETFONT, (WPARAM)hFont, FALSE);

				char sql8[] = "with idx as ( " \
					"select sm.name tbl_name, group_concat(ii.name, ', ') columns, count(1) cnt " \
					"from sqlite_master sm, pragma_index_list(sm.name) i, pragma_index_info(i.name) ii " \
					"group by sm.name, i.name) " \
					"select sm.name || '.' || fk.\"from\" 'Foreign key', " \
					"fk.\"table\" || '.' || fk.\"to\" 'Reference to', " \
					"coalesce(ck.cnt, 0) 'Wrong refs', " \
					"iif((select count(1) from idx where sm.\"name\" = idx.tbl_name and fk.\"from\" = idx.columns) > 0, 'Yes', 'No') 'Has index' " \
					"from sqlite_master sm, pragma_foreign_key_list (sm.name) fk " \
					"left join (select \"table\", parent, fkid, count(1) cnt from pragma_foreign_key_check () group by 1, 2, 3) ck " \
					"on fk.id = ck.fkid and sm.\"name\" = ck.\"table\" and fk.\"table\" = ck.parent";
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, sql8, -1, &stmt, 0))
					ListView_SetData(hListWnd, stmt);
				sqlite3_finalize(stmt);

				RECT rc, rc2;
				GetClientRect(hListWnd, &rc);

				HWND hHeader = ListView_GetHeader(hListWnd);
				int colCount = Header_GetItemCount(hHeader);
				Header_GetItemRect(hHeader, colCount - 1, &rc2);

				SetWindowPos(hWnd, 0, 0, 0, rc2.right + GetSystemMetrics(SM_CXVSCROLL) + 2 * GetSystemMetrics(SM_CXSIZEFRAME) + 2 * GetSystemMetrics(SM_CXEDGE), rc.bottom, SWP_NOZORDER | SWP_NOMOVE);
				utils::alignDialog(hWnd, hMainWnd, true);
			}
			break;

			case WM_SIZE: {
				HWND hListWnd = GetDlgItem(hWnd, IDC_DLG_FOREIGN_KEY_CHECK);
				SetWindowPos(hListWnd, 0, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOMOVE);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_NOTIFY: {
				NMHDR* pHdr = (LPNMHDR)lParam;
				if (pHdr->code == LVN_COLUMNCLICK && pHdr->idFrom == IDC_DLG_FOREIGN_KEY_CHECK) {
					NMLISTVIEW* pLV = (NMLISTVIEW*)lParam;
					return ListView_Sort(pHdr->hwndFrom, pLV->iSubItem);
				}
			}
			break;

			case WM_SYSKEYDOWN: {
				if (wParam == VK_ESCAPE)
					SendMessage(hWnd, WM_CLOSE, 0, 0);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	BOOL CALLBACK cbDlgDesktopShortcut (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				HWND hTable = GetDlgItem(hWnd, IDC_DLG_TABLENAME);
				ComboBox_AddString(hTable, TEXT("<<Entire database>>"));
				ComboBox_SetCurSel(hTable, 0);

				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select name from sqlite_master where type in ('table', 'view') order by type, name", -1, &stmt, 0)) {
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *)sqlite3_column_text(stmt, 0));
						ComboBox_AddString(hTable, name16);
						delete [] name16;
					}
				}
				sqlite3_finalize(stmt);

				EnumChildWindows(hWnd, (WNDENUMPROC)cbEnumFixEditHeights, (LPARAM)utils::getEditHeight(hWnd));
				utils::alignDialog(hWnd, hMainWnd);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_OK || wParam == IDOK) {
					HWND hTable = GetDlgItem(hWnd, IDC_DLG_TABLENAME);

					TCHAR linkName16[1024];
					GetDlgItemText(hWnd, IDC_DLG_LINK_NAME, linkName16, 1023);
					if (!_tcslen(linkName16))
						return MessageBox(hWnd, TEXT("The link name is mandatory"), NULL, MB_OK);

					bool isReadOnly = Button_GetCheck(GetDlgItem(hWnd, IDC_DLG_READ_ONLY)) == BST_CHECKED;

					HRESULT hres;
					IShellLink* psl;

					hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
					if (SUCCEEDED(hres)) {
						IPersistFile* ppf;

						TCHAR appPath16[MAX_PATH];
						GetModuleFileName(NULL, appPath16, MAX_PATH);
						psl->SetPath(appPath16);

						const char* dbPath8 = sqlite3_db_filename(db, 0);
						TCHAR* dbPath16 = utils::utf8to16(dbPath8);
						TCHAR arguments[MAX_TEXT_LENGTH] = {0};

						int idx = ComboBox_GetCurSel(hTable);
						if (idx == 0) {
							_tcscpy(arguments, dbPath16);
						} else {
							TCHAR tblname16[1024];
							ComboBox_GetText(hTable, tblname16, 1023);
							_sntprintf(arguments, MAX_TEXT_LENGTH, TEXT("\"%ls\" \"%ls\" %ls"), dbPath16, tblname16, isReadOnly ? TEXT("--readonly") : TEXT(""));
						}
						psl->SetArguments(arguments);
						delete [] dbPath16;

						hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
						if (SUCCEEDED(hres)) {
							TCHAR linkPath16[MAX_PATH];
							SHGetSpecialFolderPath(HWND_DESKTOP, linkPath16, CSIDL_DESKTOP, FALSE);
							_tcscat(linkPath16, TEXT("\\"));
							_tcscat(linkPath16, linkName16);
							_tcscat(linkPath16, TEXT(".lnk"));

							hres = ppf->Save(linkPath16, TRUE);
							ppf->Release();
							EndDialog(hWnd, DLG_OK);
						}

						psl->Release();
					}

					if (!SUCCEEDED(hres))
						MessageBox(hWnd, TEXT("Can't create a link"), NULL, MB_OK);
				}

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	// lParam = TDlgParam
	BOOL CALLBACK cbDlgTextComparison (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
			case WM_INITDIALOG: {
				TDlgParam* dp = (TDlgParam*)lParam;

				setEditorFont(GetDlgItem(hWnd, IDC_DLG_ORIGINAL));
				setEditorFont(GetDlgItem(hWnd, IDC_DLG_COMPARED));

				SetDlgItemText(hWnd, IDC_DLG_ORIGINAL, dp->s1);
				SetDlgItemText(hWnd, IDC_DLG_COMPARED, dp->s2);
				SetDlgItemText(hWnd, IDC_DLG_ORIGINAL_LABEL, dp->s3);
				SetDlgItemText(hWnd, IDC_DLG_COMPARED_LABEL, dp->s4);

				SendMessage(hWnd, WMU_COMPARE, 0, 0);
				utils::alignDialog(hWnd, GetWindow(hWnd, GW_OWNER), true);
			}
			break;

			case WM_SIZE: {
				POINTFLOAT s = utils::getDlgScale(hWnd);

				RECT rc;
				GetClientRect(hWnd, &rc);
				int W = rc.right;
				int H = rc.bottom;

				GetClientRect(hWnd, &rc);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_ORIGINAL_LABEL), 0, 5 * s.x, 12 * s.y, 108 * s.x, 14 * s.y, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_COMPARED_LABEL), 0, W - (108 + 5) * s.x, 12 * s.y, 108 * s.x, 14 * s.y, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_ORIGINAL_COUNT), 0, W / 2 - (27 + 27 + 5) * s.x, 5 * s.y, 27 * s.x, 14 * s.y, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_COMPARED_COUNT), 0, W / 2 + (27 + 5) * s.x, 5. * s.y, 27. * s.x, 14 * s.y, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_COMPARE), 0, W / 2 - 27 * s.x, 5 * s.y, 54 * s.x, 14 * s.y, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_ORIGINAL), 0, 5 * s.x, 25 * s.y, W/2 - 7.5 * s.x, H - 29 * s.y, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(GetDlgItem(hWnd, IDC_DLG_COMPARED), 0, W /2 + 2.5 * s.x, 25 * s.y, W/2 - 7.5 * s.x, H - 29 * s.y, SWP_NOZORDER | SWP_NOACTIVATE);

				InvalidateRect(hWnd, NULL, TRUE);
			}
			break;

			case WM_COMMAND: {
				if (wParam == IDC_DLG_COMPARE)
					SendMessage(hWnd, WMU_COMPARE, 0, 0);

				if (wParam == IDC_DLG_CANCEL || wParam == IDCANCEL)
					EndDialog(hWnd, DLG_CANCEL);
			}
			break;

			case WMU_COMPARE: {
				HWND hEditorA = GetDlgItem(hWnd, IDC_DLG_ORIGINAL);
				HWND hEditorB = GetDlgItem(hWnd, IDC_DLG_COMPARED);

				GETTEXTEX gt = {0};
				gt.flags = 0;
				gt.codepage = 1200;

				GETTEXTLENGTHEX gtl = {GTL_NUMBYTES, 0};
				int len = SendMessage(hEditorA, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 1200);
				TCHAR* txtA16 = new TCHAR[len + 1];
				gt.cb = len + sizeof(TCHAR);
				SendMessage(hEditorA, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)txtA16);
				char* txtA8 = utils::utf16to8(txtA16);

				len = SendMessage(hEditorB, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 1200);
				TCHAR* txtB16 = new TCHAR[len + 1];
				gt.cb = len + sizeof(TCHAR);
				SendMessage(hEditorB, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)txtB16);
				char* txtB8 = utils::utf16to8(txtB16);

				struct EditorData {
					HWND hEditorWnd;
					TCHAR *txt16;
					int len16;
					char *txt8;
					int len8;
					int dCount;
				};

				auto cbDiff = [](void *ref, dmp_operation_t op, const void *data8, uint32_t len8) {
					EditorData* ed = (EditorData*)ref;

					CHARFORMAT2 cf2;
					ZeroMemory(&cf2, sizeof(CHARFORMAT2));
					cf2.cbSize = sizeof(CHARFORMAT2) ;
					SendMessage(ed->hEditorWnd, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf2);
					cf2.dwMask = CFM_COLOR | CFM_BOLD;
					cf2.dwEffects = 0;
					cf2.crTextColor = RGB(255, 0, 0);

					if (op == DMP_DIFF_DELETE) {
						char diff8[len8 + 1]{0};
						strncpy(diff8, (char*)data8, len8);
						int len16 = MultiByteToWideChar(CP_UTF8, 0, diff8, -1, NULL, 0) - 1;

						int from16 = ed->len16 - MultiByteToWideChar(CP_UTF8, 0, (char*)data8, -1, NULL, 0) + 1;
						SendMessage(ed->hEditorWnd, EM_SETSEL, from16, from16 + len16);
						SendMessage(ed->hEditorWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &cf2);
						ed->dCount++;
					}

					return 0;
				};

				dmp_diff *diffAB, *diffBA;
				bool rcA = dmp_diff_from_strs(&diffAB, NULL, txtA8, txtB8) != 0;
				bool rcB = dmp_diff_from_strs(&diffBA, NULL, txtB8, txtA8) != 0;

				if (rcA || rcB) {
					MessageBox(hWnd, TEXT("Error occurred during comparison"), TEXT("Error"), MB_ICONERROR);
				} else {
					TCHAR cnt[64];
					EditorData ed = {hEditorA, txtA16, (int)_tcslen(txtA16), txtA8, (int)strlen(txtA8), 0};
					dmp_diff_foreach(diffAB, cbDiff, &ed);
					SendMessage(hEditorA, EM_SETSEL, 0, 0);
					SetDlgItemText(hWnd, IDC_DLG_ORIGINAL_COUNT, _itot(ed.dCount, cnt, 10));

					ed = {hEditorB, txtB16, (int)_tcslen(txtB16), txtB8, (int)strlen(txtB8), 0};
					dmp_diff_foreach(diffBA, cbDiff, &ed);
					SendMessage(hEditorB, EM_SETSEL, 0, 0);
					SetDlgItemText(hWnd, IDC_DLG_COMPARED_COUNT, _itot(ed.dCount, cnt, 10));
				}
				dmp_diff_free(diffAB);
				dmp_diff_free(diffBA);

				delete [] txtA16;
				delete [] txtA8;
				delete [] txtB16;
				delete [] txtB8;
			}
			break;

			case WM_CLOSE: {
				EndDialog(hWnd, DLG_CANCEL);
			}
			break;
		}

		return false;
	}

	bool exportExcel(const TCHAR* path16, const TCHAR* query16) {
		FILE* f = _tfopen(path16, TEXT("wb"));
		if (f == NULL)
			return false;

		int maxSheetSize = 32000;
		int sheetSize = 0;
		char* sheetData = new char[maxSheetSize]{0};

		auto maskSpecialChars = [](const char* input, char* output) {
			int res = 0;
			int pos = 0;
			for (int i = 0; i < (int)strlen(input); i++) {
				char c = input[i];
				if (strchr("<>&\"'", c) != 0) {
					strncpy(output + pos,
						c == '<' ? "&#60;" :
						c == '>' ? "&#62;" :
						c == '&' ? "&#38;" :
						c == '"' ? "&#34;" :
						"&#39;", 6);
					pos += 5;
					res++;
				} else {
					output[pos] = c;
					pos++;
				}
			}

			return res;
		};

		char* query8 = utils::utf16to8(query16);
		sqlite3_stmt *stmt;
		if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
			int colCount = sqlite3_column_count(stmt);

			char title[colCount * 512]{0};
			strcat(title, "<row>");
			for (int colNo = 0; colNo < colCount; colNo++) {
				const char* val = (const char*)sqlite3_column_name(stmt, colNo);
				char mval[4 * strlen(val) + 1]{0};
				maskSpecialChars(val, mval);

				char buf[strlen(mval) + 200];
				sprintf(buf, "<c t=\"inlineStr\"><is><t>%s</t></is></c>", mval);
				strcat(title, buf);
			}
			strcat(title, "</row>");
			strcat(sheetData, title);
			sheetSize += strlen(title);

			while (SQLITE_ROW == sqlite3_step(stmt)) {
				int size = 0;
				for(int colNo = 0; colNo < colCount; colNo++)
					size += (sqlite3_column_type(stmt, colNo) == SQLITE_TEXT ? sqlite3_column_bytes(stmt, colNo) : 20) + 256;

				char row[size + 5]{0};
				strcat(row, "<row>");

				for (int colNo = 0; colNo < colCount; colNo++) {
					int colType = sqlite3_column_type(stmt, colNo);
					size = (colType == SQLITE_TEXT ? sqlite3_column_bytes(stmt, colNo) : 20) + 256;
					char buf[size]{0};
					if (colType == SQLITE_FLOAT) {
						sprintf(buf, "<c><v>%lf</v></c>", sqlite3_column_double(stmt, colNo));
						for (int i = 0; i < (int)strlen(buf); i++)
							if (buf[i] == ',')
								buf[i] = '.';
					}

					if (colType == SQLITE_INTEGER)
						sprintf(buf, "<c><v>%i</v></c>", sqlite3_column_int(stmt, colNo));

					if (colType == SQLITE_TEXT) {
						const char* val = (const char*)sqlite3_column_text(stmt, colNo);
						char mval[4 * strlen(val) + 1]{0};
						maskSpecialChars(val, mval);
						sprintf(buf, "<c t=\"inlineStr\"><is><t>%s</t></is></c>", mval);
					}

					if (colType == SQLITE_NULL)
						sprintf(buf, "<c><v></v></c>");

					if (colType == SQLITE_BLOB)
						sprintf(buf, "<c><v>(BLOB)</v></c>");

					strcat(row, buf);
				}
				strcat(row, "</row>");

				int rlen = strlen(row);
				while (maxSheetSize - sheetSize - 100 < rlen) {
					maxSheetSize *= 2;
					sheetData = (char*)realloc(sheetData, maxSheetSize);
				}

				strncpy(sheetData + sheetSize, row, rlen + 1);
				sheetSize += rlen;
			}

			HMODULE hInstance = GetModuleHandle(0);
			HRSRC rc = FindResource(hInstance, MAKEINTRESOURCE(IDR_EXCEL), RT_RCDATA);
			HGLOBAL rcData = LoadResource(hInstance, rc);
			int templateSize = SizeofResource(hInstance, rc);
			LPVOID templateData = LockResource(rcData);
			if (templateSize > 0 && templateData) {
				BYTE data[templateSize + 1]{0};
				memcpy(data, (const char*)templateData, templateSize);

				sheetSize = strlen(sheetData);
				int offsetCRC = 0x17F1;
				int offsetFileStart = 0x1819;
				int offsetData = 0x1969;
				int offsetFileEnd = 0x1A24;
				int offsetCRC2 = 0x1C69;
				int offsetCH = 0x1CAF;
				int valueCH = 0x1A25 + strlen(sheetData);

				BYTE* header = data + offsetFileStart;
				UINT crc = ~0U;
				for (int i = 0; i < offsetData - offsetFileStart; ++i)
					crc = utils::crc32_tab[(crc ^ *header++) & 0xFF] ^ (crc >> 8);

				BYTE* ins = (BYTE*)sheetData;
				for (int i = 0; i < sheetSize; ++i)
					crc = utils::crc32_tab[(crc ^ *ins++) & 0xFF] ^ (crc >> 8);

				BYTE* footer = data + offsetData;
				for (int i = 0; i < offsetFileEnd - offsetData + 1; ++i)
					crc = utils::crc32_tab[(crc ^ *footer++) & 0xFF] ^ (crc >> 8);

				crc = crc ^ ~0U;
				int newFileSize = offsetFileEnd - offsetFileStart + 1 + sheetSize;

				for (int byteNo = 0; byteNo < 4; byteNo++) {
					(data + offsetCRC)[byteNo] = (crc >> (8 * byteNo)) & 0xff;
					(data + offsetCRC + 4)[byteNo] = (newFileSize >> (8 * byteNo)) & 0xff;
					(data + offsetCRC + 8)[byteNo] = (newFileSize >> (8 * byteNo)) & 0xff;

					(data + offsetCRC2)[byteNo] = (crc >> (8 * byteNo)) & 0xff;
					(data + offsetCRC2 + 4)[byteNo] = (newFileSize >> (8 * byteNo)) & 0xff;
					(data + offsetCRC2 + 8)[byteNo] = (newFileSize >> (8 * byteNo)) & 0xff;

					(data + offsetCH)[byteNo] = (valueCH >> (8 * byteNo)) & 0xff;
				}

				fwrite (data, sizeof(char), offsetData, f);
				fwrite (sheetData, sizeof(char), strlen(sheetData), f);
				fwrite (data + offsetData, sizeof(char), templateSize - offsetData, f);
			}
			FreeResource(rcData);
		}
		sqlite3_finalize(stmt);
		fclose(f);
		delete [] query8;
		delete [] sheetData;

		return true;
	}

	int importCSV(TCHAR* path16, TCHAR* tblname16, TCHAR* err16) {
		if (_tcslen(tblname16) == 0) {
			_sntprintf(err16, 1023, TEXT("The table name is empty"));
			return -1;
		}

		bool isColumns = prefs::get("csv-import-is-columns");
		bool isUTF8 = prefs::get("csv-import-encoding") == 0;
		bool isCreateTable = prefs::get("csv-import-is-create-table");
		bool isTruncate = !isCreateTable && prefs::get("csv-import-is-truncate");
		bool isReplace = !isCreateTable && prefs::get("csv-import-is-replace");
		bool isTrimValues = prefs::get("csv-import-trim-values");
		bool isSkipEmpty = prefs::get("csv-import-skip-empty");
		bool isAbortOnError = prefs::get("csv-import-abort-on-error");

		int iDelimiter = prefs::get("csv-import-delimiter");
		const TCHAR* delimiter = DELIMITERS[iDelimiter];

		FILE* f = _tfopen(path16, isUTF8 ? TEXT("r, ccs=UTF-8") : TEXT("r"));
		if (f == NULL) {
			_sntprintf(err16, 1023, TEXT("Error to open file: %s"), path16);
			return -1;
		}

		TCHAR create16[MAX_TEXT_LENGTH + 1]{0};
		TCHAR insert16[MAX_TEXT_LENGTH + 1]{0};
		TCHAR delete16[MAX_TEXT_LENGTH + 1]{0};

		TCHAR* schema16 = utils::getTableName(tblname16, true);
		TCHAR* tablename16 = utils::getTableName(tblname16);

		_sntprintf(create16, MAX_TEXT_LENGTH, TEXT("create table \"%ls\".\"%ls\" ("), schema16, tablename16);
		_sntprintf(insert16, MAX_TEXT_LENGTH, TEXT("%ls into \"%ls\".\"%ls\" ("), isReplace ? TEXT("replace") : TEXT("insert"), schema16, tablename16);
		_sntprintf(delete16, MAX_TEXT_LENGTH, TEXT("delete from \"%ls\".\"%ls\""), schema16, tablename16);

		delete [] tablename16;
		delete [] schema16;

		auto catQuotted = [](TCHAR* a, TCHAR* b) {
			TCHAR* tb = utils::trim(b);
			if (tb[0] == TEXT('"')) {
				_tcscat(a, tb);
			} else {
				_tcscat(a, TEXT("\""));
				TCHAR* qb = utils::replaceAll(tb, TEXT("\""), TEXT("\\\""));
				_tcscat(a, qb);
				_tcscat(a, TEXT("\""));
				delete [] qb;
			}

			delete [] tb;
		};

		int colCount = 1;
		TCHAR* header16 = csvReadLine(f);
		TCHAR* colname16 = _tcstok(header16, delimiter);
		while (colname16 != NULL) {
			if (colCount != 1) {
				_tcscat(create16, TEXT(", "));
				_tcscat(insert16, TEXT(", "));
			}

			catQuotted(create16, colname16);
			catQuotted(insert16, colname16);

			colname16 = _tcstok(NULL, delimiter);
			colCount++;
		}
		delete [] header16;
		rewind(f);

		_tcscat(create16, TEXT(");"));
		_tcscat(insert16, TEXT(") values ("));
		for (int i = 1; i < colCount; i++)
			_tcscat(insert16, i != colCount - 1 ? TEXT("?, ") : TEXT("?);"));

		bool isAutoTransaction = sqlite3_get_autocommit(db) > 0;
		if (isAutoTransaction)
			sqlite3_exec(db, "begin", NULL, 0, NULL);

		char* create8 = utils::utf16to8(create16);
		char* insert8 = utils::utf16to8(insert16);
		char* delete8 = utils::utf16to8(delete16);

		bool rc = true;
		if (isCreateTable)
			rc = SQLITE_OK == sqlite3_exec(db, create8, NULL, 0, NULL);

		if (rc && isTruncate)
			rc = SQLITE_OK == sqlite3_exec(db, delete8, NULL, 0, NULL);

		int lineNo = 0;
		int rowNo = 0;
		if (rc) {
			sqlite3_stmt *stmt;
			rc = SQLITE_OK == sqlite3_prepare_v2(db, insert8, -1, &stmt, 0);
			if (rc) {
				while(!feof (f) && rc) {
					TCHAR* line16 = csvReadLine(f);
					if (lineNo == 0 && isColumns) {
						lineNo++;
						continue;
					}

					if (_tcslen(line16) == 0)
						continue;

					int colNo = 0;

					TCHAR value[_tcslen(line16) + 1];
					bool inQuotes = false;
					int valuePos = 0;
					int i = 0;
					bool isEmptyRow = true;
					do {
						value[valuePos++] = line16[i];

						if ((!inQuotes && (line16[i] == delimiter[0] || line16[i] == TEXT('\n'))) || !line16[i + 1]) {
							value[valuePos - (line16[i + 1] != 0 || inQuotes)] = 0;
							valuePos = 0;

							TCHAR* tvalue16 = isTrimValues ? utils::trim(value) : _tcsdup(value);
							char* value8 = utils::utf16to8(tvalue16);
							dbutils::bind_variant(stmt, colNo + 1, value8);

							isEmptyRow = isEmptyRow && (strlen(value8) == 0);
							delete [] value8;
							delete [] tvalue16;

							colNo++;
						}

						if (line16[i] == TEXT('"') && line16[i + 1] != TEXT('"')) {
							valuePos--;
							inQuotes = !inQuotes;
						}

						if (line16[i] == TEXT('"') && line16[i + 1] == TEXT('"'))
							i++;

					} while (line16[++i]);

					for (int i = colNo; i < colCount; i++)
						sqlite3_bind_null(stmt, i + 1);

					if (!(isSkipEmpty && isEmptyRow)) {
						rc = (sqlite3_step(stmt) == SQLITE_DONE) || !isAbortOnError;
						if (rc)
							rowNo++;
					}
					sqlite3_reset(stmt);
					lineNo++;
					delete [] line16;
				}
			}
			sqlite3_finalize(stmt);
		}

		delete [] create8;
		delete [] insert8;
		delete [] delete8;
		fclose(f);

		if (!rc) {
			TCHAR* _err16 = utils::utf8to16(sqlite3_errmsg(db));
			_sntprintf(err16, 1023, TEXT("Error: %s"), _err16);
			delete [] _err16;
		}

		if (isAutoTransaction)
			sqlite3_exec(db, rc ? "commit" : "rollback", NULL, 0, NULL);

		return rc ? rowNo : -1;
	}

	int exportCSV(TCHAR* path16, TCHAR* query16, TCHAR* err16) {
		bool isColumns = prefs::get("csv-export-is-columns");
		int iDelimiter = prefs::get("csv-export-delimiter");
		int isUnixNewLine = prefs::get("csv-export-is-unix-line");

		const TCHAR* delimiter16 = DELIMITERS[iDelimiter];

		// Use binary mode
		// https://stackoverflow.com/questions/32143707/how-do-i-stop-fprintf-from-printing-rs-to-file-along-with-n-in-windows
		FILE* f = _tfopen(path16, TEXT("wb"));
		if (f == NULL) {
			_sntprintf(err16, 1023, TEXT("Error to open file: %s"), path16);
			return -1;
		}

		int rowCount = 0;
		char* sql8 = utils::utf16to8(query16);
		sqlite3_stmt *stmt;
		if (SQLITE_OK == sqlite3_prepare_v2(db, sql8, -1, &stmt, 0)) {
			while (isColumns || (SQLITE_ROW == sqlite3_step(stmt))) {
				int colCount = sqlite3_column_count(stmt);
				int size = 0;
				for(int i = 0; i < colCount; i++)
					size += sqlite3_column_type(stmt, i) == SQLITE_TEXT ? sqlite3_column_bytes(stmt, i) + 1 : 20;

				// https://en.wikipedia.org/wiki/Comma-separated_values
				size += colCount + 64; // add place for quotes
				TCHAR line16[size] = {0};
				for(int i = 0; i < colCount; i++) {
					if (i != 0)
						_tcscat(line16, delimiter16);

					TCHAR* value16 = utils::utf8to16(
						isColumns ? (char *)sqlite3_column_name(stmt, i) :
						sqlite3_column_type(stmt, i) != SQLITE_BLOB ? (char *)sqlite3_column_text(stmt, i) : "(BLOB)");
					TCHAR* qvalue16 = utils::replaceAll(value16, TEXT("\""), TEXT("\"\""));
					if (_tcschr(qvalue16, TEXT(',')) || _tcschr(qvalue16, TEXT('"')) || _tcschr(qvalue16, TEXT('\n'))) {
						int len = _tcslen(qvalue16) + 3;
						TCHAR val16[len + 1]{0};
						_sntprintf(val16, len, TEXT("\"%ls\""), qvalue16);
						_tcscat(line16, val16);
					} else {
						_tcscat(line16, qvalue16);
					}
					delete [] value16;
					delete [] qvalue16;
				}

				_tcscat(line16, isUnixNewLine ? TEXT("\n") : TEXT("\r\n"));
				char* line8 = utils::utf16to8(line16);
				fprintf(f, line8);
				delete [] line8;
				rowCount += !isColumns;
				isColumns = false;
			}
		} else {
			TCHAR* _err16 = utils::utf8to16(sqlite3_errmsg(db));
			_sntprintf(err16, 1023, _err16);
			delete [] _err16;
			rowCount = -1;
		}

		sqlite3_finalize(stmt);
		fclose(f);
		delete [] sql8;

		return rowCount;
	}

	bool importSqlFile(TCHAR *path16){
		char* path8 = utils::utf16to8(path16);
		char* data8 = utils::readFile(path8);
		bool rc = true;
		if (data8 != 0) {
			char* ldata8 = new char[strlen(data8) + 1];
			strcpy(ldata8, data8);
			strlwr(ldata8);
			bool hasTransaction = strstr(ldata8, "begin;") || strstr(ldata8, "begin transaction") || strstr(ldata8, "commit");
			delete [] ldata8;

			if (!hasTransaction)
				sqlite3_exec(db, "begin transaction;", NULL, 0, NULL);
			if (prefs::get("synchronous-off"))
				sqlite3_exec(db, "pragma synchronous = 0", NULL, 0, NULL);
			bool hasBOM = data8[0] == '\xEF' && data8[1] == '\xBB' && data8[2] == '\xBF';
			rc = SQLITE_OK == sqlite3_exec(db, hasBOM ? data8 + 3 : data8, NULL, 0, NULL);
			if (!rc)
				showDbError(hMainWnd);

			if (prefs::get("synchronous-off"))
				sqlite3_exec(db, "pragma synchronous = 1", NULL, 0, NULL);
			if (!hasTransaction)
				sqlite3_exec(db, rc ? "commit;" : "rollback;", NULL, 0, NULL);

			delete [] data8;
		}
		delete [] path8;

		return rc;
	}

	bool reindexDatabase() {
		sqlite3_stmt *stmt;
		BOOL rc = SQLITE_OK == sqlite3_prepare_v2(db, "select 'reindex \"' || name || '\"' from sqlite_master where type in ('table', 'index')", -1, &stmt, 0);
		while (rc && SQLITE_ROW == sqlite3_step(stmt))
			rc = SQLITE_OK == sqlite3_exec(db, (const char*)sqlite3_column_text(stmt, 0), NULL, 0, NULL);
		sqlite3_finalize(stmt);
		return rc;
	}
}
