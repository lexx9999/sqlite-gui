#include "global.h"
#include "missing.h"
#include "resource.h"
#include "prefs.h"
#include "http.h"
#include "utils.h"
#include "tools.h"
#include "dialogs.h"

#include <process.h>
#include <wininet.h>

#include "tom.h"
#include <richole.h>
#include <unknwn.h>

#define DEFINE_GUIDXXX(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) EXTERN_C const GUID name = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }
DEFINE_GUIDXXX(IID_ITextDocument, 0x8CC497C0, 0xA1DF, 0x11CE, 0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D);

#define SB_SQLITE_VERSION 0
#define SB_GUI_VERSION    1
#define SB_CARET_POSITION 2
#define SB_TRANSACTION    3
#define SB_ELAPSED_TIME   4
#define SB_SELECTED_ROW   5
#define SB_EXTENSIONS     6

const char *TYPES8[6] = {"current", "table", "view", "index", "trigger", "column"};
const TCHAR *TYPES16[6] = {TEXT("current"), TEXT("table"), TEXT("view"), TEXT("index"), TEXT("trigger"), TEXT("column")};
const TCHAR *TYPES16u[6] = {TEXT("CURRENT"), TEXT("TABLE"), TEXT("VIEW"), TEXT("INDEX"), TEXT("TRIGGER"), TEXT("COLUMN")};
const TCHAR *TYPES16p[6] = {TEXT(""), TEXT("Tables"), TEXT("Views"), TEXT("Indexes"), TEXT("Triggers"), TEXT("Columns")};
const TCHAR *transactionStates[] = {TEXT(""),TEXT(" TRN")};
COLORREF GRIDCOLORS[8]{0};

// AutoComplete
const TCHAR *SQL_KEYWORDS[] = {TEXT("abort"), TEXT("action"), TEXT("add"), TEXT("after"), TEXT("all"), TEXT("alter"), TEXT("always"), TEXT("analyze"), TEXT("and"), TEXT("as"), TEXT("asc"), TEXT("attach"), TEXT("autoincrement"), TEXT("before"), TEXT("begin"), TEXT("between"), TEXT("by"), TEXT("cascade"), TEXT("case"), TEXT("cast"), TEXT("check"), TEXT("collate"), TEXT("column"), TEXT("commit"), TEXT("conflict"), TEXT("constraint"), TEXT("create"), TEXT("cross"), TEXT("current"), TEXT("current_date"), TEXT("current_time"), TEXT("current_timestamp"), TEXT("database"), TEXT("default"), TEXT("deferrable"), TEXT("deferred"), TEXT("delete"), TEXT("desc"), TEXT("detach"), TEXT("distinct"), TEXT("do"), TEXT("drop"), TEXT("each"), TEXT("else"), TEXT("end"), TEXT("escape"), TEXT("except"), TEXT("exclude"), TEXT("exclusive"), TEXT("exists"), TEXT("explain"), TEXT("fail"), TEXT("filter"), TEXT("first"), TEXT("following"), TEXT("for"), TEXT("foreign"), TEXT("from"), TEXT("full"), TEXT("generated"), TEXT("glob"), TEXT("group"), TEXT("groups"), TEXT("having"), TEXT("if"), TEXT("ignore"), TEXT("immediate"), TEXT("in"), TEXT("index"), TEXT("indexed"), TEXT("initially"), TEXT("inner"), TEXT("insert"), TEXT("instead"), TEXT("intersect"), TEXT("into"), TEXT("is"), TEXT("isnull"), TEXT("join"), TEXT("key"), TEXT("last"), TEXT("left"), TEXT("like"), TEXT("limit"), TEXT("match"), TEXT("natural"), TEXT("no"), TEXT("not"), TEXT("nothing"), TEXT("notnull"), TEXT("null"), TEXT("nulls"), TEXT("of"), TEXT("offset"), TEXT("on"), TEXT("or"), TEXT("order"), TEXT("others"), TEXT("outer"), TEXT("over"), TEXT("partition"), TEXT("plan"), TEXT("pragma"), TEXT("preceding"), TEXT("primary"), TEXT("query"), TEXT("raise"), TEXT("range"), TEXT("recursive"), TEXT("references"), TEXT("regexp"), TEXT("reindex"), TEXT("release"), TEXT("rename"), TEXT("replace"), TEXT("restrict"), TEXT("right"), TEXT("rollback"), TEXT("row"), TEXT("rows"), TEXT("savepoint"), TEXT("select"), TEXT("set"), TEXT("table"), TEXT("temp"), TEXT("temporary"), TEXT("then"), TEXT("ties"), TEXT("to"), TEXT("transaction"), TEXT("trigger"), TEXT("unbounded"), TEXT("union"), TEXT("unique"), TEXT("update"), TEXT("using"), TEXT("vacuum"), TEXT("values"), TEXT("view"), TEXT("virtual"), TEXT("when"), TEXT("where"), TEXT("window"), TEXT("with"), TEXT("without"), TEXT("returning"), TEXT("materialized"), 0};
const TCHAR *TEMPLATES[] = {TEXT("select * from"), TEXT("select count(*) from"), TEXT("insert into"), TEXT("delete from"), TEXT("inner join"), TEXT("primary key"), TEXT("foreign key"), TEXT("case when  then  else  end"), 0};
TCHAR* PRAGMAS[MAX_ENTITY_COUNT] = {0};
TCHAR* FUNCTIONS[MAX_ENTITY_COUNT] = {0};
TCHAR* TABLES[MAX_ENTITY_COUNT] = {0};

sqlite3 *db;
HWND hMainWnd = 0, hToolbarWnd, hStatusWnd, hTreeWnd, hEditorWnd, hTabWnd, hMainTabWnd, hTooltipWnd = 0, hDialog, hSortingResultWnd, hAutoComplete, hDragWnd = 0;
HMENU hMainMenu, hDbMenu, hEditorMenu, hResultMenu, hBlobMenu, hCliMenu;
HWND hDialogs[MAX_DIALOG_COUNT]{0};
HWND hEditors[MAX_DIALOG_COUNT + MAX_TAB_COUNT]{0};

HTREEITEM treeItems[5]; // 0 - current
TCHAR treeEditName[255];
HMENU treeMenus[IDC_MENU_DISABLED]{0};

TCHAR editTableData16[255]; // filled on DataEdit Dialog; app buffer
TCHAR searchString[255]{0};
DWORD doubleClickTick = 0; // issue #50

HFONT hDefFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
HACCEL hAccel = LoadAccelerators(0, MAKEINTRESOURCE(IDA_ACCEL));
HACCEL hDlgAccel = LoadAccelerators(0, MAKEINTRESOURCE(IDA_ACCEL2));

HICON hIcons[2] = {LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(IDI_LOGO)), LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(IDI_LOGO2))};

struct TEditorTab {
	int id; // unique tab identificator
	HWND hEditorWnd;
	HWND hTabWnd; // hTabWnd.USERDATA is current ListView HWND (IDC_TAB_ROWS + resultNo); ListView.USERDATA = resultNo
	HWND hQueryListWnd;
	TCHAR* tabTooltips[MAX_RESULT_COUNT];
	TCHAR queryElapsedTimes[MAX_RESULT_COUNT][64];

	sqlite3* db;
	HANDLE thread;
	bool isPlan;
	bool isBatch;
};
TEditorTab tabs[MAX_TAB_COUNT] = {0};

struct TCliTab {
	HWND hEditorWnd;
	HWND hResultWnd;

	sqlite3* db;
	HANDLE thread;
	bool isPlan;
};
TCliTab cli{0};

// storage for cell who triggered context menu; IDM_RESULT_COPY_CELL, IDM_RESULT_COPY_ROW
struct ListViewCell {
	HWND hListWnd;
	int iItem;
	int iSubItem;
};
ListViewCell currCell;

bool isMoveX = false;
bool isMoveY = false;
int top = 0;

int currParenthesisPos[] = {-1, -1};
bool isRequireHighligth = false, isRequireParenthesisHighligth = false, isRequireOccurrenceHighlight = false;
int selStartPos = -1;

TCHAR APP_PATH[MAX_PATH]{0};

int executeCLIQuery(bool isPlan = false);
int executeEditorQuery(bool isPlan = false, bool isBatch = false, bool isOnlyCurrent = false, int vkKey = 0);
void suggestCLIQuery(int key);
void loadCLIResults(int cnt);

bool openConnection(sqlite3** _db, const char* path8, bool isReadOnly = false);
bool openDb(const TCHAR* path);
bool closeDb();
bool closeConnections();
bool isDbBusy();
void enableMainMenu();
void disableMainMenu();
void updateExecuteMenu(bool isEnable);
void updateRecentList();
void updateTree(int type = 0, TCHAR* select = NULL);
void updateSizes(bool isPadding = false);
void updateTransactionState();
int enableDbObject(const char* name8, int type);
int disableDbObject(const char* name8, int type);
HWND openDialog(int IDD, DLGPROC proc, LPARAM lParam = 0);
void saveQuery(const char* storage, const char* query);
unsigned int __stdcall checkUpdate (void* data);
bool saveResultToTable(const TCHAR* table16, int tabNo, int resultNo, int searchNext = LVNI_ALL);
bool startHttpServer();
bool stopHttpServer();

WNDPROC cbOldMainTab, cbOldMainTabRenameEdit, cbOldResultTab, cbOldTreeItemEdit, cbOldAutoComplete, cbOldListView, cbOldEdit;
LRESULT CALLBACK cbNewTreeItemEdit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK cbNewMainTab(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK cbNewMainTabRename(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK cbNewResultTab(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK cbNewAutoComplete(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK cbNewListView(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK cbMainWindow (HWND, UINT, WPARAM, LPARAM);

bool isCipherSupport = GetProcAddress(GetModuleHandle(TEXT("sqlite3.dll")), "sqlite3mc_config") != NULL;

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	setlocale(LC_ALL, "");

	GetModuleFileName(0, APP_PATH, MAX_PATH);
	PathRemoveFileSpec(APP_PATH);

	TCHAR prefPath16[MAX_PATH]{0};
	_tcscpy(prefPath16, APP_PATH);
	_tcscat(prefPath16, TEXT("\\prefs.sqlite"));
	char* prefPath8 = utils::utf16to8(prefPath16);

	bool isFirstRun = !utils::isFileExists(prefPath16);
	if (!prefs::load(prefPath8)) {
		MessageBox(0, TEXT("Settings loading failed"), TEXT("Error"), MB_OK);
		return EXIT_FAILURE;
	}
	delete [] prefPath8;

	GRIDCOLORS[SQLITE_NULL] = prefs::get("color-null");
	GRIDCOLORS[SQLITE_BLOB] = prefs::get("color-blob");
	GRIDCOLORS[SQLITE_FLOAT] = prefs::get("color-real");
	GRIDCOLORS[SQLITE_INTEGER] = prefs::get("color-integer");
	GRIDCOLORS[SQLITE_TEXT] = prefs::get("color-text");

	hEditorMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDC_MENU_EDITOR));
	hEditorMenu = GetSubMenu(hEditorMenu, 0);

	hResultMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDC_MENU_RESULT));
	hResultMenu = GetSubMenu(hResultMenu, 0);

	hBlobMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDC_MENU_BLOB));
	hBlobMenu = GetSubMenu(hBlobMenu, 0);

	hCliMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDC_MENU_CLI));
	hCliMenu = GetSubMenu(hCliMenu, 0);

	LoadLibrary(TEXT("msftedit.dll"));

	if (strlen(lpCmdLine)) {
		int nArgs = 0;
		TCHAR** args = CommandLineToArgvW(GetCommandLine(), &nArgs);
		if (nArgs == 3) {
			if (openDb(args[1])) {
				_stprintf(editTableData16, TEXT("%ls"), args[2]);
				DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_EDITDATA), hMainWnd, (DLGPROC)&dialogs::cbDlgEditData);
			}
			closeDb();
			sqlite3_shutdown();
			return 0;
		}
	}

	MSG msg;
	WNDCLASSEX wc;

	wc.hInstance = hInstance;
	wc.lpszClassName = TEXT("sqlite-gui-class");
	wc.lpfnWndProc = cbMainWindow;
	wc.style = CS_DBLCLKS;
	wc.cbSize = sizeof (WNDCLASSEX);
	wc.hIcon = hIcons[0];
	wc.hIconSm = hIcons[0];
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.lpszMenuName = 0;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = (HBRUSH) COLOR_WINDOW;

	if (!RegisterClassEx (&wc))
		return EXIT_FAILURE;

	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(icex);
	icex.dwICC = ICC_DATE_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES;
	InitCommonControlsEx(&icex);

	if (prefs::get("backup-prefs")) {
		_stprintf(prefPath16, TEXT("%ls/prefs.backup"), APP_PATH);
		DeleteFile(prefPath16);
		if (!prefs::backup())
			MessageBox(0, TEXT("Settings back up failed"), TEXT("Error"), MB_OK);
	}

	hMainWnd = CreateWindowEx (0, TEXT("sqlite-gui-class"), TEXT("sqlite-gui"), WS_OVERLAPPEDWINDOW,
		prefs::get("x"), prefs::get("y"), prefs::get("width"), prefs::get("height"),
		HWND_DESKTOP, 0, hInstance, NULL);

	hMainMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDC_MENU_MAIN));
	ModifyMenu(hMainMenu, 3, MF_BYPOSITION | MFT_RIGHTJUSTIFY, 0, TEXT("?"));
	SetMenu(hMainWnd, hMainMenu);

	TBBUTTON tbButtons [ ] = {
		{0, IDM_OPEN, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Open")},
		{1, IDM_CLOSE, TBSTATE_INDETERMINATE, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Close")},
		{-1, 0, TBSTATE_ENABLED, TBSTYLE_SEP, {0}, 0L, 0},
		{2, IDM_SAVE, TBSTATE_ENABLED, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Save")},
		{3, IDM_PLAN, TBSTATE_INDETERMINATE, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Plan")},
		{4, IDM_EXECUTE, TBSTATE_INDETERMINATE, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Execute")},
		{5, IDM_INTERRUPT, TBSTATE_HIDDEN, TBSTYLE_BUTTON, {0}, 0L, (INT_PTR)TEXT("Interrupt")}
	};

	hToolbarWnd = CreateToolbarEx (hMainWnd, WS_CHILD | WS_BORDER | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_CUSTOMERASE, IDC_TOOLBAR, 0, NULL, 0,
		tbButtons, sizeof(tbButtons)/sizeof(tbButtons[0]), 0, 0, 0, 0, sizeof (TBBUTTON));
	SendMessage(hToolbarWnd, TB_SETIMAGELIST,0, (LPARAM)ImageList_LoadBitmap(hInstance, MAKEINTRESOURCE(IDB_TOOLBAR), 0, 0, RGB(255,255,255)));
	hStatusWnd = CreateStatusWindow(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, NULL, hMainWnd, IDC_STATUSBAR);
	int sizes[7] = {80, 150, 215, 251, 365, 402, -1};
	SendMessage(hStatusWnd, SB_SETPARTS, 7, (LPARAM)&sizes);

	char version8[32];
	sprintf(version8, " SQLite: %s", sqlite3_libversion());
	TCHAR* version16 = utils::utf8to16(version8);
	SendMessage(hStatusWnd, SB_SETTEXT, SB_SQLITE_VERSION, (LPARAM)version16);
	delete [] version16;
	TCHAR guiVersion[32]{0};
	_stprintf(guiVersion, TEXT(" GUI: %ls"), TEXT(GUI_VERSION));
	SendMessage(hStatusWnd, SB_SETTEXT, SB_GUI_VERSION, (LPARAM)guiVersion);

	hTreeWnd = CreateWindowEx(0, WC_TREEVIEW, NULL, WS_VISIBLE | WS_CHILD | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT  | WS_DISABLED | TVS_EDITLABELS, 0, 0, 100, 100, hMainWnd, (HMENU)IDC_TREE, hInstance,  NULL);
	hMainTabWnd = CreateWindowEx(0, WC_STATIC, NULL, WS_VISIBLE | WS_CHILD | SS_NOTIFY, 100, 0, 100, 100, hMainWnd, (HMENU)IDC_MAINTAB, hInstance,  NULL);
	createTooltip(hMainWnd);

	SendMessage(hMainTabWnd, WM_SETFONT, (LPARAM)hDefFont, true);
	cbOldMainTab = (WNDPROC)SetWindowLong(hMainTabWnd, GWL_WNDPROC, (LONG)cbNewMainTab);
	for (int i = 0; i < (prefs::get("restore-editor") ? prefs::get("editor-tab-count") : 1); i++) {
		SendMessage(hMainTabWnd, WMU_TAB_ADD, 0, 0);
	}
	SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, 0, 0);

	cli.hEditorWnd = CreateWindowEx(0, TEXT("RICHEDIT50W"), NULL, WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | ES_NOHIDESEL, 0, 0, 100, 100, hMainWnd, (HMENU)IDC_EDITOR, GetModuleHandle(0),  NULL);
	SendMessage(cli.hEditorWnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_KEYEVENTS | ENM_MOUSEEVENTS | ENM_DROPFILES);
	SetWindowLong(cli.hEditorWnd, GWL_USERDATA, 0);
	SendMessage(cli.hEditorWnd, EM_EXLIMITTEXT, 0, 32767 * 10);
	if (prefs::get("word-wrap"))
		toggleWordWrap(cli.hEditorWnd);
	cli.hResultWnd = CreateWindowEx(WS_EX_STATICEDGE, TEXT("RICHEDIT50W"), NULL, WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | ES_READONLY, 0, 0, 100, 100, hMainWnd, (HMENU)IDC_CLI_RESULT, GetModuleHandle(0),  NULL);
	SendMessage(cli.hResultWnd, EM_SETBKGNDCOLOR, 0, RGB(0, 0, 0));
	SendMessage(cli.hResultWnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
	CreateWindow(WC_LISTBOX, NULL, WS_CHILD, 0, 0, 150, 200, cli.hResultWnd, (HMENU)IDC_CLI_RAWDATA, GetModuleHandle(0), NULL);
	DragAcceptFiles(cli.hEditorWnd, true);

	hDbMenu = GetSubMenu(hMainMenu, 0);
	if (!isCipherSupport)
		RemoveMenu(GetSubMenu(hMainMenu, 0), IDM_ENCRYPTION, MF_BYCOMMAND);
	updateRecentList();

	HMENU hMenu;
	int idcs [] = {IDC_MENU_TABLEVIEW, IDC_MENU_INDEXTRIGGER, IDC_MENU_TABLE, IDC_MENU_VIEW, IDC_MENU_INDEX, IDC_MENU_TRIGGER, IDC_MENU_COLUMN, IDC_MENU_DISABLED};
	for (int idc : idcs) {
		hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(idc));
		treeMenus[idc] = GetSubMenu(hMenu, 0);
	}

	TCHAR wfPath16[MAX_PATH] = {0};
	_stprintf(wfPath16, TEXT("%ls/extensions/odbc.dll"), APP_PATH);
	if (!utils::isFileExists(wfPath16)) {
		HMENU hToolMenu = GetSubMenu(hMainMenu, 2);
		if (RemoveMenu(GetSubMenu(hToolMenu, 4), IDM_IMPORT_ODBC, MF_BYCOMMAND) == 0 ||
			RemoveMenu(GetSubMenu(hToolMenu, 5), IDM_EXPORT_ODBC, MF_BYCOMMAND) == 0)
			MessageBox(hMainWnd, TEXT("Import-Export menu bug"), 0, MB_OK);
	}

	EnumChildWindows(hMainWnd, (WNDENUMPROC)cbEnumChildren, (LPARAM)ACTION_SETDEFFONT);
	setTreeFont(hTreeWnd);

	if (strlen(lpCmdLine)) {
		int nArgs = 0;
		TCHAR** args = CommandLineToArgvW(GetCommandLine(), &nArgs);
		openDb(args[1]);
	} else if (isFirstRun) {
		TCHAR demoDb[MAX_PATH];
		_stprintf(demoDb, TEXT("%ls\\bookstore.sqlite"), APP_PATH);
		if (utils::isFileExists(demoDb)) {
			TCHAR buf[MAX_TEXT_LENGTH];
			LoadString(GetModuleHandle(NULL), IDS_WELCOME, buf, MAX_TEXT_LENGTH);
			SetWindowText(hEditorWnd, buf);
			openDb(demoDb);
		}
	} else if (prefs::get("restore-db")) {
		int recent = GetMenuItemID(hDbMenu, 4);
		if (recent && !(GetMenuState(hDbMenu, IDM_RECENT, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED)))
			PostMessage(hMainWnd, WM_COMMAND, recent, 0);
	}

	if (prefs::get("restore-editor") && !isFirstRun) {
		char* text8 = prefs::get("editor-cli-text", "");
		TCHAR* text16 = utils::utf8to16(text8);
		SetWindowText(cli.hEditorWnd, text16);
		delete [] text8;
		delete [] text16;

		for (int tabNo = 0; tabNo < prefs::get("editor-tab-count"); tabNo++) {
			char key[60] = {0};
			sprintf(key, "editor-text-%i", tabNo);
			char* text8 = prefs::get(key, "");
			TCHAR* text16 = utils::utf8to16(text8);
			SetWindowText(tabs[tabNo].hEditorWnd, text16);
			delete [] text8;
			delete [] text16;

			TCHAR def[60] = {0};
			_stprintf(def, TEXT("Editor #%i"), tabNo + 1);
			sprintf(key, "editor-name-%i", tabNo);
			text8 = prefs::get(key, "");
			text16 = utils::utf8to16(text8);
			SendMessage(hMainTabWnd, WMU_TAB_SET_TEXT, tabNo, (LPARAM)(_tcslen(text16) ? text16: def));
			delete [] text8;
			delete [] text16;
		}
		SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, prefs::get("editor-tab-current"), 0);
	}

	hAutoComplete = CreateWindowEx(WS_EX_TOPMOST, WC_LISTBOX, NULL, WS_POPUP | WS_BORDER | WS_VSCROLL, 0, 0, 170, 200, hMainWnd, (HMENU)0, GetModuleHandle(0), NULL);
	SendMessage(hAutoComplete, WM_SETFONT, (LPARAM)hDefFont, true);
	cbOldAutoComplete = (WNDPROC)SetWindowLong(hAutoComplete, GWL_WNDPROC, (LONG)cbNewAutoComplete);

	setEditorFont(hEditorWnd);
	setEditorFont(cli.hResultWnd);
	ShowWindow (hMainWnd, prefs::get("maximized") == 1 ? SW_MAXIMIZE : SW_SHOW);
	SetFocus(hEditorWnd);

	if (prefs::get("check-update")) {
		// Use thread to prevent GUI blocking on startup
		HANDLE hThread = (HANDLE)_beginthreadex(0, 0, &checkUpdate, 0, 0, 0);
		CloseHandle(hThread);
	}

	// https://docs.microsoft.com/en-us/windows/win32/dlgbox/using-dialog-boxes
	while (GetMessage(&msg, NULL, 0, 0)) {
		bool isDialogMessage = false;
		HWND hDlg = 0;
		for (int i = 0; i < MAX_DIALOG_COUNT && !isDialogMessage; i++) {
			isDialogMessage = hDialogs[i] && IsWindow(hDialogs[i]) && IsDialogMessage(hDialogs[i], &msg);
			hDlg = hDialogs[i];
		}

		if (isDialogMessage && TranslateAccelerator(hDlg, hDlgAccel, &msg))
			continue;

		if (!isDialogMessage && TranslateAccelerator(hMainWnd, hAccel, &msg))
			continue;

		if (!isDialogMessage) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	// Preventing buffer clearing on exit
	TCHAR* txt = utils::getClipboardText();
	utils::setClipboardText(txt);
	delete [] txt;

	return msg.wParam;
}

LRESULT CALLBACK cbMainWindow (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CLOSE: {
			if (!closeDb())
				return 0;

			prefs::setSyncMode(0);

			if (prefs::get("restore-editor")) {
				int tabCurrent = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				prefs::set("editor-tab-current", tabCurrent);

				int size = GetWindowTextLength(cli.hEditorWnd);
				TCHAR text16[size + 1] = {0};
				GetWindowText(cli.hEditorWnd, text16, size + 1);
				char *text8 = utils::utf16to8(text16);
				prefs::set("editor-cli-text", text8);
				delete [] text8;

				int tabCount = SendMessage(hMainTabWnd, WMU_TAB_GET_COUNT, 0, 0);
				prefs::set("editor-tab-count", tabCount);

				for (int tabNo = 0; tabNo < tabCount; tabNo++) {
					HWND hEditorWnd = tabs[tabNo].hEditorWnd;
					int size = GetWindowTextLength(hEditorWnd);
					TCHAR text16[size + 1] = {0};
					GetWindowText(hEditorWnd, text16, size + 1);
					char key[80] = {0};
					sprintf(key, "editor-text-%i", tabNo);
					char *text8 = utils::utf16to8(text16);
					prefs::set(key, text8);
					delete [] text8;

					TCHAR name16[80] = {0};
					SendMessage(hMainTabWnd, WMU_TAB_GET_TEXT, tabNo, (LPARAM)name16);
					sprintf(key, "editor-name-%i", tabNo);
					char *name8 = utils::utf16to8(name16);
					prefs::set(key, name8);
					delete [] name8;
				}
			}

			if (prefs::save()) {
				prefs::setSyncMode(1);
				sqlite3_close(prefs::db);
			} else {
				MessageBox(hWnd, TEXT("Settings saving failed"), TEXT("Error"), MB_OK);
			}

			sqlite3_shutdown();
			PostQuitMessage (0);
		}
		break;

		case WM_ACTIVATEAPP: {
			if (!wParam)
				ShowWindow(hAutoComplete, SW_HIDE);
		}
		break;

		case WM_SIZE: {
			SendMessage(hToolbarWnd, WM_SIZE, 0, 0);
			SendMessage(hStatusWnd, WM_SIZE, 0, 0);

			int h = HIWORD(lParam);
			if (hTabWnd && IsWindowVisible(hTabWnd) && h) {
				RECT rc;
				GetClientRect(hTabWnd, &rc);

				if (rc.bottom && rc.right) {
					int oldH = prefs::get("splitter-height") + rc.bottom + 52 /* ~ Top menu + toolbar + statusbar */;
					prefs::set("splitter-height", prefs::get("splitter-height") * h / oldH);
				}
			}

			updateSizes(true);
		}
		break;

		case WM_LBUTTONDOWN: {
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);

			isMoveX = (abs(x - prefs::get("splitter-width")) < 10);
			isMoveY = (x > prefs::get("splitter-width") + 10) && (abs(y - top - prefs::get("splitter-height")) < 10);

			// Drag by tab nc-area
			if (!isMoveY && (x > prefs::get("splitter-width") + 10)) {
				POINT p{x, y};
				ClientToScreen(hMainWnd, &p);
				ScreenToClient(hTabWnd, &p);

				RECT rc = {0, 0, -10, 30};
				int tabCount = TabCtrl_GetItemCount(hTabWnd);
				if (tabCount != 0)
					TabCtrl_GetItemRect(hTabWnd, tabCount - 1, &rc);
				isMoveY = (p.x > rc.right + 10) && (p.y <= rc.bottom) && (p.y >= rc.top);
			}

			if (isMoveX || isMoveY)
				SetCapture(hMainWnd);
		}
		break;

		case WM_LBUTTONUP: {
			if (isMoveX || isMoveY) {
				updateSizes(true);
				ReleaseCapture();
			}

			if (hDragWnd) {
				ReleaseCapture();
				ShowCursor(true);

				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				POINT p{x, y};
				if (ChildWindowFromPointEx(hMainWnd, p, CWP_SKIPDISABLED | CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT) == hEditorWnd) {
					TCHAR buf[255], qbuf[257];
					GetWindowText(hDragWnd, buf, 255);

					if (!HIWORD(GetKeyState(VK_CONTROL)) || GetWindowLong(hDragWnd, GWL_USERDATA) == COLUMN) {
						bool isAlphaNum = !_istdigit(buf[0]);
						for (int i = 0; isAlphaNum && (i < (int)_tcslen(buf)); i++)
							isAlphaNum = _istalnum(buf[i]) || (buf[i] == TEXT('_'));
						if (!isAlphaNum)
							_stprintf(qbuf, TEXT("\"%ls\""), buf);

						SendMessage(hEditorWnd, EM_REPLACESEL, true, (LPARAM)(isAlphaNum ? buf : qbuf));
					} else {
						TCHAR res[MAX_TEXT_LENGTH];
						_stprintf(res, buf);
						if (DLG_OK == DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_DROP), hWnd, (DLGPROC)&dialogs::cbDlgDrop, (LPARAM)res))
							SendMessage(hEditorWnd, EM_REPLACESEL, true, (LPARAM)res);
					}
				}

				DestroyWindow(hDragWnd);
				hDragWnd = 0;
			}

			isMoveX = FALSE;
			isMoveY = FALSE;
		}
		break;

		case WM_MOUSEMOVE: {
			LONG x = GET_X_LPARAM(lParam);
			LONG y = GET_Y_LPARAM(lParam);

			if (hDragWnd) {
				SetWindowPos(hDragWnd, 0, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

				POINT p{x, y};
				if (ChildWindowFromPointEx(hMainWnd, p, CWP_SKIPDISABLED | CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT) == hEditorWnd) {
					ClientToScreen(hMainWnd, &p);
					ScreenToClient(hEditorWnd, &p);
					int pos = SendMessage(hEditorWnd, EM_CHARFROMPOS, 0, (LPARAM)&p);
					SendMessage(hEditorWnd, EM_SETSEL, pos, pos);
					SetFocus(hEditorWnd);
				}
				InvalidateRect(hDragWnd, NULL, true);
				return true;
			}

			isMoveX = isMoveX && (wParam == MK_LBUTTON);
			isMoveY = isMoveY && (wParam == MK_LBUTTON);

			if (isMoveX) {
				prefs::set("splitter-width", x - 3);
				updateSizes();
			}

			if (isMoveY) {
				prefs::set("splitter-height", y - 3 - top);
				updateSizes();
			}
		}
		break;

		case WM_NCLBUTTONDBLCLK: {
			WINDOWPLACEMENT wp = {};
			wp.length = sizeof(WINDOWPLACEMENT);
			GetWindowPlacement(hMainWnd, &wp);
			prefs::set("maximized", (int)(wp.showCmd != SW_SHOWMAXIMIZED));
			return DefWindowProc (hWnd, msg, wParam, lParam);
		}
		break;

		case WM_SYSCOMMAND: {
			if (wParam == SC_MAXIMIZE || wParam == SC_RESTORE)
				prefs::set("maximized", wParam == SC_MAXIMIZE);
			return DefWindowProc (hWnd, msg, wParam, lParam);
		}
		break;

		case WM_EXITSIZEMOVE: {
			RECT rc;
			GetWindowRect(hMainWnd, &rc);
			prefs::set("x", rc.left);
			prefs::set("y", rc.top);
			prefs::set("width", rc.right - rc.left);
			prefs::set("height", rc.bottom - rc.top);
			updateSizes(true);
		}
		break;

		case WMU_UNREGISTER_DIALOG: {
			for (int i = 0; i < MAX_DIALOG_COUNT; i++)
				hDialogs[i] = (HWND)wParam == hDialogs[i] ? 0 : hDialogs[i];

			HWND hNextWnd = hMainWnd;
			for (int i = 0; i < MAX_DIALOG_COUNT && hNextWnd == hMainWnd; i++)
				hNextWnd = hDialogs[i] != 0 ? hDialogs[i] : hNextWnd;

			SetActiveWindow(hNextWnd);
			if (hNextWnd == hMainWnd)
				SetFocus(hEditorWnd);
		}
		break;

		// wParam - type
		case WMU_OBJECT_CREATED: {
			// With hope that the last record is a new object
			int type = (int)wParam;
			sqlite3_stmt *stmt;
			sqlite3_prepare_v2(db, "select name from sqlite_master where type = ?1", -1, &stmt, 0);
			sqlite3_bind_text(stmt, 1, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
			while(SQLITE_ROW == sqlite3_step(stmt)) {
				TCHAR* name16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
				updateTree(type, name16);
				delete [] name16;
			}
			sqlite3_finalize(stmt);
		}
		break;

		// lParam = isDatabase encrypted
		case WMU_SET_ICON: {
			SendMessage(hMainWnd, WM_SETICON, 0, (LPARAM)hIcons[lParam]);
		}
		break;

		case WMU_UPDATE_CARET_INFO: {
			TCHAR buf16[255];
			CHARRANGE cr{0};
			SendMessage(hEditorWnd, EM_EXGETSEL, 0, (LPARAM)&cr);
			int x = cr.cpMin - SendMessage(hEditorWnd, EM_LINEINDEX, (WPARAM)-1, 0);
			int y = SendMessage(hEditorWnd, EM_LINEFROMCHAR,(WPARAM)-1, 0) + 1;
			_stprintf(buf16, TEXT(" %i:%i"), y, x + 1);
			SendMessage(hStatusWnd, SB_SETTEXT, SB_CARET_POSITION, (LPARAM)buf16);
		}
		break;

		case WMU_UPDATE_ROWNO: {
			HWND hListWnd = (HWND)GetWindowLong(hTabWnd, GWL_USERDATA);
			TCHAR buf16[255]{0};
			int pos = hListWnd && IsWindow(hListWnd) ? ListView_GetNextItem(hListWnd, -1, LVNI_SELECTED) : -1;
			if (pos != - 1)
				_stprintf(buf16, TEXT(" %i"), pos + 1);
			SendMessage(hStatusWnd, SB_SETTEXT, SB_SELECTED_ROW, (LPARAM)buf16);
		}
		break;

		case WMU_GET_CURRENT_RESULT: {
			return GetWindowLong(hTabWnd, GWL_USERDATA);
		}
		break;

		case WM_CONTEXTMENU: {
			POINT p = {LOWORD(lParam), HIWORD(lParam)};
			bool isContextKey = p.x == 65535 && p.y == 65535;
			if ((HWND)wParam == hEditorWnd && !isContextKey)
				TrackPopupMenu(hEditorMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hMainWnd, NULL);

			if ((HWND)wParam == cli.hResultWnd && !isContextKey)
				TrackPopupMenu(hCliMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hMainWnd, NULL);

			if ((HWND)wParam == hTreeWnd) {
				// update selected item on right click
				if (!isContextKey) {
					POINT p2 = {0};
					GetCursorPos(&p2);
					ScreenToClient(hTreeWnd, &p2);
					TVHITTESTINFO thi = {p2,TVHT_ONITEM};
					HTREEITEM hItem = TreeView_HitTest(hTreeWnd, &thi);
					TreeView_SelectItem(hTreeWnd, hItem);
					if (!hItem)
						return 0;

					treeItems[0] = thi.hItem;
				}

				TVITEM ti;
				ti.hItem = treeItems[0];
				ti.mask = TVIF_PARAM;
				TreeView_GetItem(hTreeWnd, &ti);

				if (isContextKey) {
					RECT rc = {0};
					//TreeView_GetItemRect(hTreeWnd, ti.hItem, &rc, TRUE); // is the macros problem?
					*(HTREEITEM*)&rc = ti.hItem;
					SendMessage(hTreeWnd, TVM_GETITEMRECT, FALSE, (LPARAM)&rc);
					p.x = rc.left + 10;
					p.y = rc.top + 10;
					ClientToScreen(hTreeWnd, &p);
				}

				int type = ti.lParam;
				if (type) {
					BOOL isCut = TreeView_GetItemState(hTreeWnd, ti.hItem, TVIS_CUT) & TVIS_CUT;
					int idc = type == TABLE ? IDC_MENU_TABLE :
						type == VIEW ? IDC_MENU_VIEW :
						type == INDEX && !isCut ? IDC_MENU_INDEX :
						type == TRIGGER && !isCut  ? IDC_MENU_TRIGGER :
						type == -TABLE || type == -VIEW ? IDC_MENU_TABLEVIEW :
						type == -INDEX || type == -TRIGGER ? IDC_MENU_INDEXTRIGGER :
						type == COLUMN && TreeView_GetParent(hTreeWnd, TreeView_GetParent(hTreeWnd, ti.hItem)) == treeItems[TABLE] ? IDC_MENU_COLUMN :
						isCut ? IDC_MENU_DISABLED :
						0;

					if (type == VIEW) {
						TCHAR name16[256] = {0};
						TV_ITEM tv;
						tv.mask = TVIF_TEXT;
						tv.hItem = treeItems[0];
						tv.pszText = name16;
						tv.cchTextMax = 256;

						if(!TreeView_GetItem(hTreeWnd, &tv))
							return 0;

						char* name8 = utils::utf16to8(name16);
						sqlite3_stmt *stmt;
						sqlite3_prepare_v2(db, "select 1 from sqlite_master where tbl_name = ?1 and type = 'trigger' and instr(lower(sql), 'instead of ') > 0", -1, &stmt, 0);
						sqlite3_bind_text(stmt, 1, name8, strlen(name8), SQLITE_TRANSIENT);
						ModifyMenu(treeMenus[idc], IDM_EDIT_DATA, MF_BYCOMMAND | MF_STRING, IDM_EDIT_DATA, SQLITE_ROW == sqlite3_step(stmt) ? TEXT("Edit data") : TEXT("View data"));
						sqlite3_finalize(stmt);
						delete [] name8;
					}

					if (idc)
						TrackPopupMenu(treeMenus[idc], TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hMainWnd, NULL);
				}
			}
		}
		break;

		case WM_COMMAND: {
			WORD cmd = LOWORD(wParam);

			if (cmd == IDM_EXIT)
				SendMessage(hMainWnd, WM_CLOSE, 0, 0);

			if (cmd == IDM_OPEN) {
				TCHAR path16[MAX_PATH]{0};
				if (HIWORD(GetKeyState(VK_SHIFT)))
					return openDb(TEXT("file::memory:?cache=shared"));

				if (utils::openFile(path16, TEXT("Databases (*.sqlite, *.sqlite3, *.db, *.db3)\0*.sqlite;*.sqlite3;*.db;*.db3\0All\0*.*\0"), hWnd))
					return openDb(path16);
			}

			if (cmd == IDM_SAVE_AS) {
				TCHAR path16[MAX_PATH]{0};
				char* dbname8 = utils::getFileName(sqlite3_db_filename(db, 0), true);
				TCHAR* dbname16 = utils::utf8to16(dbname8);
				_stprintf(path16, dbname16);
				delete [] dbname8;
				delete [] dbname16;

				if (utils::saveFile(path16, TEXT("Databases (*.sqlite, *.sqlite3, *.db, *.db3)\0*.sqlite;*.sqlite3;*.db;*.db3\0All\0*.*\0"), TEXT("sqlite"), hWnd)) {
					sqlite3_stmt *stmt;
					bool rc = SQLITE_OK == sqlite3_prepare_v2(db, "vacuum into ?1", -1, &stmt, 0);
					if (rc) {
						char* path8 = utils::utf16to8(path16);
						sqlite3_bind_text(stmt, 1, path8, strlen(path8), SQLITE_TRANSIENT);
						delete [] path8;
						rc = SQLITE_DONE == sqlite3_step(stmt);
					}
					sqlite3_finalize(stmt);

					if (!rc)
						showDbError(hMainWnd);
				}
			}

			if (cmd == IDM_CLOSE)
				closeDb();

			if (cmd == IDM_ATTACH) {
				if (isDbBusy())
					return false;

				TCHAR path16[MAX_PATH]{0};
				if (utils::openFile(path16, TEXT("Databases (*.sqlite, *.sqlite3, *.db, *.db3)\0*.sqlite;*.sqlite3;*.db;*.db3\0All\0*.*\0"), hWnd)) {
					char* path8 = utils::utf16to8(path16);
					attachDb(&db, path8);
					delete [] path8;
				}
			}

			if (cmd == IDM_ENCRYPTION) {
				if (isDbBusy())
					return false;

				if (DLG_OK == DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_ENCRYPTION), hMainWnd, (DLGPROC)&dialogs::cbDlgEncryption))
					closeConnections();
			}

			if (cmd == IDM_SETTINGS && DLG_OK == DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_SETTINGS), hMainWnd, (DLGPROC)&dialogs::cbDlgSettings)) {
				setEditorFont(hEditorWnd);
				setTreeFont(hTreeWnd);

				if (!prefs::get("use-highlight")) {
					for (int i = 0; i < MAX_TAB_COUNT; i++) {
						if (tabs[i].id)
							setEditorColor(tabs[i].hEditorWnd, RGB(0, 0, 0), true);
					}
				}

				stopHttpServer();
				startHttpServer();

				GRIDCOLORS[SQLITE_TEXT] = prefs::get("color-text");
				GRIDCOLORS[SQLITE_NULL] = prefs::get("color-null");
				GRIDCOLORS[SQLITE_BLOB] = prefs::get("color-blob");
				GRIDCOLORS[SQLITE_INTEGER] = prefs::get("color-integer");
				GRIDCOLORS[SQLITE_FLOAT] = prefs::get("color-real");
			}

			if (cmd == IDM_EXECUTE || cmd == IDM_PLAN || cmd == IDM_EXECUTE_BATCH) {
				int currTab = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				return currTab == -1 ? executeCLIQuery(cmd == IDM_PLAN) : executeEditorQuery(cmd == IDM_PLAN, cmd == IDM_EXECUTE_BATCH);
			}

			if (cmd == IDM_EXECUTE_CURRENT || cmd == IDM_PLAN_CURRENT) {
				int currTab = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				if (currTab != -1)
					return executeEditorQuery(cmd == IDM_PLAN_CURRENT, false, true);
			}

			if (cmd == IDM_SHORTCUTS) {
				DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_SHORTCUTS), hMainWnd, (DLGPROC)dialogs::cbDlgShortcuts, 0);
				return true;
			}

			if (cmd == IDM_INTERRUPT) {
				int tabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				sqlite3_interrupt(tabNo >= 0 ? tabs[tabNo].db : cli.db);
			}

			// By accelerator
			if (cmd == IDM_OPEN_EDITOR)
				SendMessage(hMainTabWnd, WMU_TAB_ADD, 0, 0);

			if (cmd == IDM_CLOSE_EDITOR) {
				int tabCount = SendMessage(hMainTabWnd, WMU_TAB_GET_COUNT, 0, 0);
				int currTab = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				SendMessage(hMainTabWnd, WMU_TAB_DELETE, currTab, 0);
				SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, currTab == tabCount - 1 ? currTab - 1 : currTab, 0);
			}

			if (cmd == IDM_PREV_EDITOR || cmd == IDM_NEXT_EDITOR) {
				int tabCount = SendMessage(hMainTabWnd, WMU_TAB_GET_COUNT, 0, 0);
				int currTab = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, (currTab + (cmd == IDM_PREV_EDITOR ? - 1 : 1) + tabCount) % tabCount, 1);
			}

			if (cmd == IDM_NEXT_RESULT) {
				int pos = TabCtrl_GetCurSel(hTabWnd);
				if (pos != -1) {
					int count = TabCtrl_GetItemCount(hTabWnd);
					TabCtrl_SetCurFocus(hTabWnd, (pos + 1) % count);
				}
			}

			if (cmd == IDM_CHANGE_FOCUS) {
				HWND hWndList[] = {hTreeWnd, hEditorWnd, (HWND)GetWindowLong(hTabWnd, GWL_USERDATA)};
				HWND hFocus = GetFocus();
				int idx = hFocus == hTreeWnd ? 0 : hFocus == hEditorWnd ? 1 : 2;
				HWND hNextWnd = hWndList[(idx + 1 + 3) % 3];

				if (hNextWnd == hWndList[2] && !TabCtrl_GetItemCount(hTabWnd))
				hNextWnd = hFocus == hTreeWnd ? hEditorWnd : hTreeWnd;

				SetFocus(hNextWnd);
			}

			if (cmd == IDM_PROCESS_TAB) {
				int start, end;
				HWND hEditorWnd = (HWND)lParam;
				SendMessage(hEditorWnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);

				SetWindowRedraw(hEditorWnd, false);
				POINT scrollPos{0};
				SendMessage(hWnd, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPos);

				int dir = HIWORD(GetKeyState(VK_SHIFT)) ? -1 : +1;
				int startLineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, start, 0);
				int endLineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, end - (start != end), 0);
				const TCHAR* indent = dialogs::INDENTS[prefs::get("editor-indent")];

				int shift0 = 0;
				for (int currLineNo = startLineNo; currLineNo < endLineNo + 1; currLineNo++) {
					int currLineIdx = SendMessage(hEditorWnd, EM_LINEINDEX, currLineNo, 0);
					int currLineSize = SendMessage(hEditorWnd, EM_LINELENGTH, currLineIdx, 0);

					int shift = 0;
					if (dir == -1 || currLineNo == startLineNo) {
						TCHAR currLine[currLineSize + 1]{0};
						currLine[0] = currLineSize;
						SendMessage(hEditorWnd, EM_GETLINE, currLineNo, (LPARAM)currLine);

						// Is the selection start inside or outside the text?
						if (currLineNo == startLineNo) {
							bool isOut = true;
							for (int pos = 0; pos < start - currLineIdx; pos++)
								isOut = isOut && (currLine[pos] == TEXT(' ') || currLine[pos] == TEXT('\t'));

							if (!isOut) {
								if (dir == 1)
									SendMessage(hEditorWnd, EM_REPLACESEL, true, (LPARAM)indent);

								SendMessage(hWnd, EM_SETSCROLLPOS, 0, (LPARAM)&scrollPos);
								SetWindowRedraw(hEditorWnd, true);
								InvalidateRect(hEditorWnd, NULL, true);
								return true;
							}
						}

						shift = 0;
						if (dir == -1) {
							for (int i = 0; (i < currLineSize) && (i < (int)_tcslen(indent)) && (currLine[i] == TEXT(' ')); i++)
								shift++;

							shift = currLine[0] == TEXT('\t') ? 1 : shift;
						}

						if (currLineNo == startLineNo)
							shift0 = shift;
					}

					SendMessage(hEditorWnd, EM_SETSEL, currLineIdx, currLineIdx + shift);
					SendMessage(hEditorWnd, EM_REPLACESEL, true, (LPARAM)(dir == 1 ? indent : TEXT("")));
				}

				if (start == end) {
					shift0 = dir == 1 ? _tcslen(indent) : start != SendMessage(hEditorWnd, EM_LINEINDEX, startLineNo, 0) ? -shift0 : 0;
					SendMessage(hEditorWnd, EM_SETSEL, start + shift0, start + shift0);
				} else {
					start = SendMessage(hEditorWnd, EM_LINEINDEX, startLineNo, 0);
					end = SendMessage(hEditorWnd, EM_LINEINDEX, endLineNo, 0);
					end += SendMessage(hEditorWnd, EM_LINELENGTH, end, 0);
					PostMessage(hEditorWnd, EM_SETSEL, start, end); // Selection will be dropped for dialog editor if use SendMessage
				}

				SendMessage(hWnd, EM_SETSCROLLPOS, 0, (LPARAM)&scrollPos);
				SetWindowRedraw(hEditorWnd, true);
				InvalidateRect(hEditorWnd, NULL, true);

				isRequireHighligth = false;
				isRequireParenthesisHighligth = false;

				return true;
			}

			if (cmd == IDM_ESCAPE) {
				HWND hActiveWnd = GetActiveWindow();
				if (hActiveWnd != hMainWnd)
					return SendMessage(hActiveWnd, WM_CLOSE, 0, 0);

				if (hDragWnd) {
					DestroyWindow(hDragWnd);
					hDragWnd = 0;
					ReleaseCapture();
					ShowCursor(true);
					return true;
				}

				if (!SendMessage(hToolbarWnd, TB_ISBUTTONHIDDEN, IDM_INTERRUPT, 0))
					return SendMessage(hWnd, WM_COMMAND, IDM_INTERRUPT, 0);

				if(prefs::get("exit-by-escape") && !IsWindowVisible(hAutoComplete) && !GetDlgItem(hMainTabWnd, IDC_TAB_EDIT) && !TreeView_GetEditControl(hTreeWnd))
					SendMessage(hMainWnd, WM_CLOSE, 0, 0);
			}

			if (cmd == IDM_QUERY_DATA || cmd == IDM_EDIT_DATA || cmd == IDM_ERASE_DATA || cmd == IDM_ADD_COLUMN) {
				TCHAR name16[256] = {0};
				TV_ITEM tv;
				tv.mask = TVIF_TEXT | TVIF_HANDLE | TVIF_PARAM;
				tv.hItem = treeItems[0];
				tv.pszText = name16;
				tv.cchTextMax = 256;

				if(!TreeView_GetItem(hTreeWnd, &tv))
					break;

				if (cmd == IDM_QUERY_DATA) {
					if (SendMessage(hMainTabWnd, WMU_TAB_GET_COUNT, 0, 0) >= MAX_TAB_COUNT)
						return MessageBox(hMainWnd, TEXT("Unable to show data. Too many open tabs."), NULL, MB_OK | MB_ICONWARNING);

					TCHAR query[256]{0};

					int tabNo = SendMessage(hMainTabWnd, WMU_TAB_ADD, 0, 0);
					SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, tabNo, 0);
					SendMessage(hMainTabWnd, WMU_TAB_SET_TEXT, tabNo, (WPARAM)name16);

					int len = GetWindowTextLength(hEditorWnd);
					SendMessage(hEditorWnd, EM_SETSEL, (WPARAM)len, (LPARAM)len);
					_stprintf(query, TEXT("%lsselect * from \"%ls\";\n"), len > 0 ? TEXT("\n") : TEXT(""), name16);
					len += _tcslen(query);
					SendMessage(hEditorWnd, EM_REPLACESEL, TRUE, (LPARAM)query);
					SendMessage(hEditorWnd, EM_SETSEL, (WPARAM)len, (LPARAM)len);
					SetFocus(hEditorWnd);

					executeEditorQuery();
					SendMessage(hMainWnd, WM_SIZE, 0, 0);
					SendMessage(hTabWnd, TCM_SETCURFOCUS, TabCtrl_GetItemCount(hTabWnd) - 1, 0);

					SetFocus(hEditorWnd);
					return 1;
				}

				if (cmd == IDM_EDIT_DATA) {
					_stprintf(editTableData16, (_tcschr(name16, TEXT('.')) == NULL) ? TEXT("%ls") : TEXT("\"%ls\""), name16);
					openDialog(IDD_EDITDATA, (DLGPROC)&dialogs::cbDlgEditData);
				}

				if (cmd == IDM_ERASE_DATA) {
					TCHAR query16[256 + _tcslen(name16)];
					_stprintf(query16, TEXT("Are you sure you want to delete all data from \"%ls\"?"), name16);
					if (MessageBox(hMainWnd, query16, TEXT("Delete confirmation"), MB_OKCANCEL) == IDOK) {
						_stprintf(query16, TEXT("delete from \"%ls\";"), name16);
						char* query8 = utils::utf16to8(query16);
						if (SQLITE_OK != sqlite3_exec(db, query8, NULL, 0 , 0))
							showDbError(hMainWnd);
						delete [] query8;
					}
					SetFocus(hTreeWnd);
				}

				if (cmd == IDM_ADD_COLUMN) {
					_tcscpy(editTableData16, name16);
					int rc = DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_ADD_COLUMN), hMainWnd, (DLGPROC)&dialogs::cbDlgAddColumn);
					if (rc != DLG_CANCEL)
						updateTree(TABLE);
					SetFocus(hTreeWnd);
				}
			}

			if (cmd == IDM_RENAME)
				(void)TreeView_EditLabel(hTreeWnd, treeItems[0]);

			if (cmd == IDM_REFRESH) {
				TV_ITEM tv{0};
				tv.mask = TVIF_PARAM | TVIF_TEXT;
				tv.hItem = treeItems[0];
				TreeView_GetItem(hTreeWnd, &tv);
				updateTree(abs(tv.lParam));
			}

			if (cmd == IDM_ADD || cmd == IDM_DELETE || cmd == IDM_VIEW || cmd == IDM_EDIT ||
				cmd == IDM_ENABLE || cmd == IDM_DISABLE || cmd == IDM_ENABLE_ALL || cmd == IDM_DISABLE_ALL) {
				TCHAR name16[256] = {0};
				TV_ITEM tv;
				tv.mask = TVIF_PARAM | TVIF_TEXT;
				tv.hItem = treeItems[0];
				tv.cchTextMax = 255;
				tv.pszText = name16;
				if(!TreeView_GetItem(hTreeWnd, &tv) || !tv.lParam)
					return 0;

				int type = abs(tv.lParam);
				bool rc = true;
				if (cmd == IDM_ADD) {
					if (type == TABLE) {
						if (DLG_OK != DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_ADD_TABLE), hMainWnd, (DLGPROC)&dialogs::cbDlgAddTable, (LPARAM)name16))
							return false;
					} else {
						openDialog(IDD_ADDEDIT, (DLGPROC)&dialogs::cbDlgAddEdit, MAKELPARAM(IDM_ADD, type));
						return true;
					}
				}

				if (cmd == IDM_DELETE) {
					BOOL isCut = (type == INDEX || type == TRIGGER) && TreeView_GetItemState(hTreeWnd, treeItems[0], TVIS_CUT) & TVIS_CUT;

					if (type == COLUMN)
						_tcstok(name16, TEXT(":"));

					TCHAR msg16[255];
					_stprintf(msg16, TEXT("Are you sure you want to delete the %ls \"%ls\"?"), TYPES16[type], name16);
					if (MessageBox(hMainWnd, msg16, TEXT("Delete confirmation"), MB_OKCANCEL) == IDOK) {
						if (type == COLUMN) {
							TCHAR tblname16[255];
							HTREEITEM hParent = TreeView_GetParent(hTreeWnd, treeItems[0]);
							TVITEM ti;
							ti.hItem = hParent;
							ti.mask = TVIF_PARAM | TVIF_TEXT;
							ti.pszText = tblname16;
							ti.cchTextMax = 255;
							TreeView_GetItem(hTreeWnd, &ti);
							if (ti.lParam != TABLE)
								return 0;

							TCHAR query16[1024];
							_stprintf(query16, TEXT("alter table \"%ls\" drop column \"%ls\""), tblname16, name16);
							char* query8 = utils::utf16to8(query16);
							int rc = sqlite3_exec(db, query8, NULL, 0 , 0);
							delete [] query8;

							if (SQLITE_OK == rc) {
								TreeView_DeleteItem(hTreeWnd, tv.hItem);
								TreeView_SelectItem(hTreeWnd, hParent);
								SetFocus(hTreeWnd);
							} else {
								showDbError(hMainWnd);
							}
						} else {
							char* name8 = utils::utf16to8(name16);
							char query8[1024];
							if (isCut)
								sprintf(query8, "delete from disabled where name = \"%s\" and type = \"%s\" and dbpath = \"%s\"", name8, TYPES8[type], sqlite3_db_filename(db, 0));
							else
								sprintf(query8, "drop %s \"%s\"", TYPES8[type], name8);

							if (SQLITE_OK == sqlite3_exec(isCut ? prefs::db : db, query8, 0, 0, 0)) {
								HTREEITEM hItem = TreeView_GetNextItem(hTreeWnd, treeItems[0], TVGN_PREVIOUS);
								if (!hItem)
									hItem = TreeView_GetNextItem(hTreeWnd, treeItems[0], TVGN_NEXT);
								if (!hItem)
									hItem = TreeView_GetNextItem(hTreeWnd, treeItems[0], TVGN_PARENT);

								TreeView_DeleteItem(hTreeWnd, treeItems[0]);
								TreeView_SelectItem(hTreeWnd, hItem);
								treeItems[0] = hItem;
							} else {
								showDbError(hWnd);
								rc = false;
							}
							delete [] name8;
						}
					}
					return 0;
				}

				if (cmd == IDM_EDIT) {
					TV_ITEM tv;
					tv.mask = TVIF_HANDLE | TVIF_PARAM;
					tv.hItem = treeItems[0];
					if(!TreeView_GetItem(hTreeWnd, &tv) || !tv.lParam || tv.lParam == COLUMN)
						return 0;

					_stprintf(editTableData16, TEXT("%ls"), name16);
					openDialog(IDD_ADDEDIT, (DLGPROC)&dialogs::cbDlgAddEdit, MAKELPARAM(IDM_EDIT, type));
					return true;
				}

				if (cmd == IDM_VIEW) {
					sqlite3_stmt *stmt;
					sqlite3_prepare_v2(prefs::db, "select sql, rowid from disabled where name = ?1 and type = ?2 and dbpath = ?3", -1, &stmt, 0);
					char* name8 = utils::utf16to8(name16);
					sqlite3_bind_text(stmt, 1, name8, strlen(name8), SQLITE_TRANSIENT);
					delete [] name8;
					sqlite3_bind_text(stmt, 2, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
					const char* dbpath = sqlite3_db_filename(db, 0);
					sqlite3_bind_text(stmt, 3, dbpath, strlen(dbpath), SQLITE_TRANSIENT);

					if (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* DDL16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
						DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_DDL), hWnd, (DLGPROC)&dialogs::cbDlgDDL, (LPARAM)DDL16);
						delete [] DDL16;
						rc = false;
					}
					sqlite3_finalize(stmt);
				}

				if (cmd == IDM_ENABLE) {
					char* name8 = utils::utf16to8(name16);
					rc = enableDbObject(name8, type);
					delete [] name8;
				}

				if (cmd == IDM_DISABLE) {
					char* name8 = utils::utf16to8(name16);
					rc = disableDbObject(name8, type);
					delete [] name8;
				}

				if (cmd == IDM_ENABLE_ALL) {
					sqlite3_stmt *stmt;
					sqlite3_prepare_v2(prefs::db, "select name from disabled where type = ?1 and dbpath = ?2", -1, &stmt, 0);
					sqlite3_bind_text(stmt, 1, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
					const char* dbpath = sqlite3_db_filename(db, 0);
					sqlite3_bind_text(stmt, 2, dbpath, strlen(dbpath), SQLITE_TRANSIENT);
					while (SQLITE_ROW == sqlite3_step(stmt))
						enableDbObject((const char*)sqlite3_column_text(stmt, 0), type);
					sqlite3_finalize(stmt);
				}

				// Uses names8 list to prevent database lock
				if (cmd == IDM_DISABLE_ALL) {
					char names8[MAX_TEXT_LENGTH]{0};

					sqlite3_stmt *stmt;
					sqlite3_prepare_v2(db, "select name from sqlite_master where type = ?1 and sql is not null", -1, &stmt, 0);
					sqlite3_bind_text(stmt, 1, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						strcat(names8, (const char*)sqlite3_column_text(stmt, 0));
						strcat(names8, ";");
					}
					sqlite3_finalize(stmt);

					char* name8;
					name8 = strtok (names8, ";");
					while (name8 != NULL) {
						disableDbObject(name8, type);
						name8 = strtok (NULL, ";");
					}
				}

				if (rc)
					updateTree(type, name16);
				SetFocus(hTreeWnd);
			}

			if (cmd >= IDM_RECENT && cmd <= IDM_RECENT + prefs::get("recent-count")) {
				TCHAR name16[MAX_PATH];
				GetMenuString(hDbMenu, cmd, name16, MAX_PATH, MF_BYCOMMAND);
				openDb(name16);
			}

			if (cmd == IDM_SAVE && IDYES == MessageBox(0, TEXT("Save query?"), TEXT("Confirmation"), MB_YESNO)) {
				CHARRANGE range;
				SendMessage(hEditorWnd, EM_EXGETSEL, 0, (LPARAM)&range);

				bool isSelection = range.cpMin != range.cpMax;
				int size =  isSelection ? range.cpMax - range.cpMin + 1 : GetWindowTextLength(hEditorWnd);
				if (size > 0) {
					TCHAR text16[size + 1]{0};
					if (SendMessage(hEditorWnd, isSelection ? EM_GETSELTEXT : WM_GETTEXT, size + 1, (LPARAM)text16)) {
						TCHAR* ttext16 = utils::trim(text16);
						char* text8 = utils::utf16to8(ttext16);
						saveQuery("gists", text8);
						delete [] ttext16;
						delete [] text8;
					}
				}
				SetFocus(hEditorWnd);
			}

			if (cmd == IDM_RESULT_CHART || cmd == IDM_RESULT_COPY_CELL || cmd == IDM_RESULT_COPY_ROW || cmd == IDM_RESULT_EXPORT)
				onListViewMenu(currCell.hListWnd, currCell.iItem, currCell.iSubItem, cmd);

			if (cmd == IDM_RESULT_AS_TABLE) {
				HWND hListWnd = currCell.hListWnd;
				TCHAR table16[255];
				_tcscpy(table16, TEXT("result"));
				if (DLG_CANCEL == DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_TABLENAME), hMainWnd, (DLGPROC)&dialogs::cbDlgTableName, (LPARAM)table16))
					return false;

				int tabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				int resultNo = GetWindowLong(hListWnd, GWL_USERDATA);
				int searchNext = ListView_GetSelectedCount(hListWnd) < 2 ? LVNI_ALL : LVNI_SELECTED;
				if (saveResultToTable(table16, tabNo, resultNo, searchNext)) {
					MessageBeep(0);
					updateTree(TABLE, table16);
					SetFocus(hTreeWnd);
				}
				return true;
			}

			if (cmd == IDM_RESULT_EXCEL) {
				HWND hListWnd = currCell.hListWnd;
				int currTabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				HWND hCurrListWnd = (HWND)GetWindowLong(tabs[currTabNo].hTabWnd, GWL_USERDATA);
				int currResultNo = GetWindowLong(hCurrListWnd, GWL_USERDATA);

				TCHAR tmpPath16[MAX_PATH], path16[MAX_PATH];
				GetTempPath(MAX_PATH, tmpPath16);
				SYSTEMTIME st;
				GetLocalTime(&st);
				_stprintf(path16, TEXT("%sQuery-result-%.2u-%.2u.xlsx"), tmpPath16, st.wHour, st.wMinute);

				int searchNext = ListView_GetSelectedCount(hListWnd) < 2 ? LVNI_ALL : LVNI_SELECTED;
				sqlite3_exec(db, "drop table temp.excel;", 0, 0, 0);
				saveResultToTable(TEXT("temp.excel"), currTabNo, currResultNo, searchNext);
				if (tools::exportExcel(path16, TEXT("select * from temp.excel")))
					ShellExecute(0, TEXT("open"), path16, NULL, NULL, 0);
				else
					MessageBox(hMainWnd, TEXT("Can't export data to Excel"), NULL, MB_OK);

				return true;
			}

			if (cmd >= IDM_RESULT_COMPARE && cmd <= IDM_RESULT_COMPARE + MAX_COMPARE_RESULT) {
				int currTabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
				HWND hCurrListWnd = (HWND)GetWindowLong(tabs[currTabNo].hTabWnd, GWL_USERDATA);
				int currResultNo = GetWindowLong(hCurrListWnd, GWL_USERDATA);

				HMENU hCompareMenu = GetSubMenu(hResultMenu, 9);
				MENUITEMINFO mii{0};
				mii.cbSize = sizeof(MENUITEMINFO);
				mii.fMask = MIIM_DATA;
				GetMenuItemInfo(hCompareMenu, cmd, false, &mii);

				int tabNo = LOWORD(mii.dwItemData);
				int resultNo = HIWORD(mii.dwItemData);

				TCHAR title[1024]{0};
				TCHAR buf[256];
				SendMessage(hMainTabWnd, WMU_TAB_GET_TEXT, currTabNo, (LPARAM)buf);
				_tcscat(title, buf);
				_tcscat(title, TEXT(" > "));
				TabCtrl_GetItemText(tabs[currTabNo].hTabWnd, currResultNo, buf, 255);
				_tcscat(title, buf);
				_tcscat(title, TEXT("   vs   "));
				SendMessage(hMainTabWnd, WMU_TAB_GET_TEXT, tabNo, (LPARAM)buf);
				_tcscat(title, buf);
				_tcscat(title, TEXT(" > "));
				TabCtrl_GetItemText(tabs[tabNo].hTabWnd, resultNo, buf, 255);
				_tcscat(title, buf);

				sqlite3_exec(db, "drop table temp.result1; drop table temp.result2;", 0, 0, 0);
				saveResultToTable(TEXT("temp.result1"), currTabNo, currResultNo);
				saveResultToTable(TEXT("temp.result2"), tabNo, resultNo);
				openDialog(IDD_RESULTS_COMPARISON, (DLGPROC)&dialogs::cbDlgResultsComparison, (LPARAM)title);
				return true;
			}

			if (cmd == IDM_EDITOR_COMMENT)
				toggleComment(hEditorWnd);

			if (cmd == IDM_EDITOR_CUT)
				SendMessage(hEditorWnd, WM_CUT, 0, 0);

			if (cmd == IDM_EDITOR_COPY)
				SendMessage(hEditorWnd, WM_COPY, 0, 0);

			if (cmd == IDM_EDITOR_PASTE)
				pasteText (hEditorWnd);

			if (cmd == IDM_EDITOR_DELETE)
				SendMessage (hEditorWnd, EM_REPLACESEL, TRUE, 0);

			if (cmd == IDM_EDITOR_FIND) {
				DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_FIND), hWnd, (DLGPROC)dialogs::cbDlgFind, (LPARAM)hEditorWnd);
				SetFocus(hEditorWnd);
				return true;
			}

			if (cmd == IDM_EDITOR_FORMAT)
				formatQuery(hEditorWnd);

			if (cmd == IDM_CLI_COPY)
				SendMessage(cli.hResultWnd, WM_COPY, 0, 0);

			if (cmd == IDM_CLI_CUT) {
				CHARRANGE range;
				SendMessage(cli.hResultWnd, EM_EXGETSEL, 0, (LPARAM)&range);
				int size =  range.cpMax - range.cpMin;
				if (size > 1) {
					TCHAR buf16[size + 1]{0};
					SendMessage(cli.hResultWnd, EM_GETSELTEXT, size + 1, (LPARAM)buf16);
					utils::setClipboardText(buf16);
				}

				SendMessage(cli.hResultWnd, EM_REPLACESEL, 0, 0);
			}

			if (cmd == IDM_CLI_CLEAR_ALL)
				SetWindowText(cli.hResultWnd, 0);

			if (cmd == IDM_ABOUT || cmd == IDM_TIPS || cmd == IDM_EXTENSIONS || cmd == IDM_HOTKEYS) {
				HMENU hMenu = GetSubMenu(hMainMenu, 3);
				TCHAR title[255];
				GetMenuString(hMenu, cmd, title, 255, MF_BYCOMMAND);
				TCHAR buf[MAX_TEXT_LENGTH];
				LoadString(GetModuleHandle(NULL), cmd == IDM_ABOUT ? IDS_ABOUT : cmd == IDM_TIPS ? IDS_TIPS : cmd == IDM_HOTKEYS ? IDS_HOTKEYS : IDS_EXTENSIONS, buf, MAX_TEXT_LENGTH);
				MessageBox(hMainWnd, buf, title, MB_OK);
			}

			if (cmd == IDM_HOMEPAGE)
				ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-gui"), 0, 0 , SW_SHOW);

			if (cmd == IDM_WIKI)
				ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-gui/wiki"), 0, 0 , SW_SHOW);

			if (cmd == IDM_DEMODB_BOOKSTORE)
				ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-gui/raw/master/etc/bookstore.sqlite"), 0, 0 , SW_SHOW);

			if (cmd == IDM_DEMODB_CHINOOK)
				ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-gui/raw/master/etc/chinook.sqlite"), 0, 0 , SW_SHOW);

			if (cmd == IDM_DEMODB_NORTHWIND)
				ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-gui/raw/master/etc/northwind.sqlite"), 0, 0 , SW_SHOW);

			if (cmd == IDM_DEMODB_WORLD)
				ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-gui/raw/master/etc/world.sqlite"), 0, 0 , SW_SHOW);

			if (cmd == IDM_SQLITE_HOMEPAGE)
				ShellExecute(0, 0, TEXT("https://www.sqlite.org/index.html"), 0, 0 , SW_SHOW);

			if (cmd == IDM_TUTORIAL1)
				ShellExecute(0, 0, TEXT("https://www.sqlitetutorial.net/"), 0, 0 , SW_SHOW);

			if (cmd == IDM_TUTORIAL2)
				ShellExecute(0, 0, TEXT("https://www.tutorialspoint.com/sqlite/"), 0, 0 , SW_SHOW);

			if (cmd == IDM_HISTORY || cmd == IDM_GISTS) {
				DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_QUERYLIST), hMainWnd, (DLGPROC)&dialogs::cbDlgQueryList, (LPARAM)cmd);
				SetFocus(hEditorWnd);
			}

			if (cmd == IDM_IMPORT_SQL) {
				TCHAR path16[MAX_PATH];
				if(utils::openFile(path16, TEXT("*.sql\0*.sql\0All\0*.*\0"), hWnd) && tools::importSqlFile(path16)) {
					updateTree();
					MessageBox(hMainWnd, TEXT("Done"), TEXT("Info"), MB_OK);
				}
			}

			if (cmd == IDM_IMPORT_CSV) {
				TCHAR path16[MAX_PATH];
				if(utils::openFile(path16, TEXT("CSV files\0*.csv\0All\0*.*\0"), hWnd)) {
					int rc = DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_IMPORT_CSV), hMainWnd, (DLGPROC)&tools::cbDlgImportCSV, (LPARAM)path16);
					if (rc != -1) {
						TCHAR msg16[255];
						_stprintf(msg16, TEXT("Done.\nImported %i rows."), rc);
						MessageBox(hMainWnd, msg16, TEXT("Info"), MB_OK);
						updateTree(TABLE, path16);
						SetFocus(hTreeWnd);
					}
				}
			}

			if (cmd == IDM_IMPORT_JSON) {
				TCHAR path16[MAX_PATH];
				if(utils::openFile(path16, TEXT("Json files\0*.json\0All\0*.*\0"), hWnd)) {
					if (DLG_OK == DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_IMPORT_JSON), hMainWnd, (DLGPROC)&tools::cbDlgImportJSON, (LPARAM)path16)) {
						MessageBox(hMainWnd, TEXT("Done"), TEXT("Info"), MB_OK);
						updateTree(TABLE, path16);
						SetFocus(hTreeWnd);
					}
				}
			}

			if (cmd == IDM_IMPORT_ODBC || cmd == IDM_EXPORT_ODBC) {
				bool isExport = cmd == IDM_EXPORT_ODBC;
				DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_EXPORT_IMPORT_ODBC), hMainWnd, (DLGPROC)&tools::cbDlgExportImportODBC, isExport);
				if (!isExport)
					updateTree();
			}

			if ((cmd == IDM_EXPORT_CSV) && (DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_EXPORT_CSV), hMainWnd, (DLGPROC)tools::cbDlgExportCSV) == DLG_OK))
				MessageBox(hMainWnd, TEXT("Done"), TEXT("Info"), MB_OK);

			if ((cmd == IDM_EXPORT_JSON) && (DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_EXPORT_JSON), hMainWnd, (DLGPROC)tools::cbDlgExportJSON) == DLG_OK))
				MessageBox(hMainWnd, TEXT("Done"), TEXT("Info"), MB_OK);

			if ((cmd == IDM_EXPORT_EXCEL) && (DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_EXPORT_JSON), hMainWnd, (DLGPROC)tools::cbDlgExportExcel) == DLG_OK))
				MessageBox(hMainWnd, TEXT("Done"), TEXT("Info"), MB_OK);

			if ((cmd == IDM_EXPORT_SQL) && (DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_EXPORT_SQL), hMainWnd, (DLGPROC)tools::cbDlgExportSQL) == DLG_OK))
				MessageBox(hMainWnd, TEXT("Done"), TEXT("Info"), MB_OK);

			if (cmd == IDM_GENERATE_DATA)
				DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_GENERATE_DATA), hMainWnd, (DLGPROC)&tools::cbDlgDataGenerator);

			if (cmd == IDM_DATABASE_DIAGRAM)
				DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_DATABASE_DIAGRAM), hMainWnd, (DLGPROC)&tools::cbDlgDatabaseDiagram);

			if (cmd == IDM_COMPARE_DATABASE)
				DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_COMPARE_DATABASE), hMainWnd, (DLGPROC)&tools::cbDlgCompareDatabase);

			if (cmd == IDM_DATABASE_SEARCH)
				DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_DATABASE_SEARCH), hMainWnd, (DLGPROC)&tools::cbDlgDatabaseSearch);

			if (cmd == IDM_STATISTICS)
				DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_TOOL_STATISTICS), hMainWnd, (DLGPROC)&tools::cbDlgStatistics);

			if (cmd == IDM_CHECK_INTEGRITY) {
				sqlite3_stmt* stmt;
				int rc = SQLITE_OK == sqlite3_prepare_v2(db, "select integrity_check = 'ok' from pragma_integrity_check;", -1, &stmt, NULL) && SQLITE_ROW == sqlite3_step(stmt);
				if (rc) {
					MessageBox(hMainWnd, sqlite3_column_int(stmt, 0) == 1 ? TEXT("Ok") : TEXT("Fail"), TEXT("Check result"), MB_OK);
				} else {
					showDbError(hMainWnd);
				}
				sqlite3_finalize(stmt);
			}

			if (cmd == IDM_VACUUM && IDYES == MessageBox(0, TEXT("The operation may take some time. Continue?"), TEXT("Confirmation"), MB_YESNO)) {
				if (SQLITE_OK == sqlite3_exec(db, "vacuum;", 0, 0, 0))
					MessageBox(hMainWnd, TEXT("Done"), TEXT("Vacuum database"), MB_OK);
				else
					showDbError(hMainWnd);
			}

			if (cmd == IDM_REINDEX && IDYES == MessageBox(0, TEXT("The operation may take some time. Continue?"), TEXT("Confirmation"), MB_YESNO)) {
				if (tools::reindexDatabase())
					MessageBox(hMainWnd, TEXT("Done"), TEXT("Reindex database"), MB_OK);
				else
					showDbError(hMainWnd);
			}

			if (LOWORD(wParam) == IDC_EDITOR && HIWORD(wParam) == EN_CHANGE && prefs::get("use-highlight") && !isRequireHighligth) {
				PostMessage(hMainWnd, WMU_HIGHLIGHT, 0, 0);
				isRequireHighligth = true;
			}

			if (LOWORD(wParam) == IDC_EDITOR && HIWORD(wParam) == EN_KILLFOCUS && IsWindowVisible(hTooltipWnd))
				hideTooltip();

			if (wParam == IDCANCEL)
				SendMessage(hMainWnd, WM_CLOSE, 0, 0);
		}
		break;

		case WM_NOTIFY: {
			NMHDR* pHdr = (LPNMHDR)lParam;
			MSGFILTER* pF = (MSGFILTER*)lParam;
			TCHAR wndClass[256];
			GetClassName(pHdr->hwndFrom, wndClass, 256);

			if (pHdr->hwndFrom == hTabWnd && pHdr->code == TCN_SELCHANGE) {
				EnumChildWindows(hMainWnd, (WNDENUMPROC)cbEnumChildren, (LPARAM)ACTION_UPDATETAB);
				SendMessage(hMainWnd, WMU_UPDATE_ROWNO, 0, 0);
			}

			if ((HWND)GetParent(pHdr->hwndFrom) == hTabWnd && pHdr->code == (DWORD)NM_CLICK && GetAsyncKeyState(VK_MENU)) {
				NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
				return ListView_ShowRef(pHdr->hwndFrom, ia->iItem, ia->iSubItem);
			}

			if ((HWND)GetParent(pHdr->hwndFrom) == hTabWnd && pHdr->code == (DWORD)NM_CLICK && HIWORD(GetKeyState(VK_CONTROL))) {
				NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
				if (ia->iItem == -1 || ListView_GetSelectedCount(pHdr->hwndFrom) > 1)
					return true;

				TCHAR buf[MAX_TEXT_LENGTH]{0};
				ListView_GetItemText(pHdr->hwndFrom, ia->iItem, ia->iSubItem, buf, MAX_TEXT_LENGTH);
				DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_VIEWDATA_VALUE), hMainWnd, (DLGPROC)dialogs::cbDlgViewEditDataValue, (LPARAM)buf);
			}

			if ((HWND)GetParent(pHdr->hwndFrom) == hTabWnd && pHdr->code == (DWORD)NM_DBLCLK) {
				NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
				currCell = {ia->hdr.hwndFrom, ia->iItem, ia->iSubItem};
				if (ia->iItem != -1)
					DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_ROW), hMainWnd, (DLGPROC)&dialogs::cbDlgRow, MAKELPARAM(ROW_VIEW, 1));
			}

			if ((HWND)GetParent(pHdr->hwndFrom) == hTabWnd && pHdr->code == (DWORD)LVN_KEYDOWN) {
				NMLVKEYDOWN* kd = (LPNMLVKEYDOWN) lParam;
				if (kd->wVKey == 0x43 && GetKeyState(VK_CONTROL)) {// Ctrl + C
					currCell = {kd->hdr.hwndFrom, 0, 0};
					PostMessage(hMainWnd, WM_COMMAND, MAKEWPARAM(IDM_RESULT_COPY_ROW, 0), 0);
				}

				if (kd->wVKey == 0x41 && GetKeyState(VK_CONTROL)) // Ctrl + A
					ListView_SetItemState(kd->hdr.hwndFrom, -1, LVIS_SELECTED, LVIS_SELECTED);

				if (kd->wVKey == VK_RETURN) {
					currCell = {pHdr->hwndFrom, ListView_GetNextItem(pHdr->hwndFrom, -1, LVNI_SELECTED), 0};
					if (currCell.iItem != -1)
						DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_ROW), hMainWnd, (DLGPROC)&dialogs::cbDlgRow, MAKELPARAM(ROW_VIEW, 1));
					SetFocus(pHdr->hwndFrom);
				}

				bool isNum = kd->wVKey >= 0x31 && kd->wVKey <= 0x39;
				bool isNumPad = kd->wVKey >= 0x61 && kd->wVKey <= 0x69;
				if ((isNum || isNumPad) && GetKeyState(VK_CONTROL)) // Ctrl + 1-9
					return ListView_Sort(pHdr->hwndFrom, kd->wVKey - (isNum ? 0x31 : 0x61) + 1 );
			}

			if ((HWND)GetParent(pHdr->hwndFrom) == hTabWnd && pHdr->code == (DWORD)LVN_ITEMCHANGED)
				SendMessage(hMainWnd, WMU_UPDATE_ROWNO, 0, 0);

			if (pHdr->hwndFrom == hEditorWnd && pHdr->code == WM_RBUTTONDOWN) {
				POINT p;
				GetCursorPos(&p);
				TrackPopupMenu(hEditorMenu, 0, p.x, p.y, 0, hMainWnd, NULL);
			}

			if (pF->nmhdr.hwndFrom == hEditorWnd && pF->msg == WM_KEYDOWN && pF->wParam == VK_SPACE && HIWORD(GetKeyState(VK_CONTROL))) { // Ctrl + Space
				return SendMessage(hMainWnd, WMU_SHOW_INFO, 1, 0);
			}

			// Query shortcut Alt + F1, Ctrl + 0, Ctrl + 1, ...
			if (pF->nmhdr.hwndFrom == hEditorWnd && (
				(pF->msg == WM_KEYDOWN && pF->wParam >= 0x30 && pF->wParam <= 0x39 && HIWORD(GetKeyState(VK_CONTROL))) ||
				(pF->msg == WM_SYSKEYDOWN && pF->wParam == VK_F1 && HIWORD(GetKeyState(VK_MENU)))
				)) {
				executeEditorQuery(false, false, false, pF->wParam);
				return true;
			}

			if (pF->nmhdr.hwndFrom == hEditorWnd && pF->msg == WM_KEYUP && pF->wParam == VK_CONTROL && IsWindowVisible(hTooltipWnd))
				hideTooltip();

			if (pHdr->hwndFrom == hEditorWnd && pHdr->code == EN_SELCHANGE) {
				SendMessage(hMainWnd, WMU_UPDATE_CARET_INFO, 0, 0);

				SELCHANGE *pSc = (SELCHANGE*)lParam;
				isRequireParenthesisHighligth = pSc->seltyp == SEL_EMPTY;
				isRequireOccurrenceHighlight = (isRequireOccurrenceHighlight && pSc->seltyp == SEL_EMPTY) || pSc->seltyp != SEL_EMPTY;
				if (pSc->seltyp == SEL_EMPTY)
					selStartPos = pSc->chrg.cpMin;
				if (isRequireParenthesisHighligth || isRequireOccurrenceHighlight)
					PostMessage(hWnd, WMU_HIGHLIGHT, 0, 0);

				fixQuoteSelection(hEditorWnd, pSc);
				return true;
			}

			if ((pHdr->idFrom == IDC_EDITOR || pHdr->idFrom == IDC_CLI_EDITOR) && pHdr->code == EN_DROPFILES) {
				TCHAR path16[MAX_PATH];
				HDROP drop = (HDROP)((ENDROPFILES*)lParam)->hDrop;
				DragQueryFileW(drop, 0, path16, MAX_PATH);
				DragFinish(drop);

				FILE* f = _tfopen(path16, TEXT("r, ccs=UTF-8"));
				if (f == NULL) {
					MessageBox(hWnd, TEXT("Error to open file"), NULL, MB_OK);
					return 0;
				}

				fseek(f, 0L, SEEK_END);
				long size = ftell(f);
				rewind(f);

				if (size > 0) {
					TCHAR* data16 = new TCHAR[size]{0};
					fread(data16, size, 1, f);

					int pos = ((ENDROPFILES*)lParam)->cp;
					SendMessage(pHdr->hwndFrom, EM_SETSEL, pos, pos);
					SendMessage(pHdr->hwndFrom, EM_REPLACESEL, true, (LPARAM)data16);
					delete [] data16;
				} else {
					MessageBox(0, TEXT("Only text files are supported"), 0, MB_OK);
				}

				fclose(f);
				return 0;
			}

			if (pHdr->hwndFrom == hTreeWnd && (pHdr->code == (DWORD)NM_DBLCLK || pHdr->code == (DWORD)NM_RETURN)) {
				HTREEITEM hParent = TreeView_GetParent(hTreeWnd, treeItems[0]);
				if (hParent != NULL) {
					int cmd = hParent == treeItems[TABLE] || hParent == treeItems[VIEW] ? IDM_EDIT_DATA :
						TreeView_GetItemState(hTreeWnd, treeItems[0], TVIS_CUT) & TVIS_CUT ? IDM_VIEW : IDM_EDIT;
					PostMessage(hMainWnd, WM_COMMAND, cmd, 0);
				}
				return 1;
			}

			if (pHdr->hwndFrom == hTreeWnd && pHdr->code == (DWORD)TVN_KEYDOWN) {
				NMTVKEYDOWN* pKd = (LPNMTVKEYDOWN) lParam;
				if ((pKd->wVKey == VK_DELETE)) {
					TVITEM ti{0};
					ti.hItem = treeItems[0];
					ti.mask = TVIF_PARAM;
					TreeView_GetItem(hTreeWnd, &ti);
					if (ti.lParam == TABLE || ti.lParam == VIEW || ti.lParam == TRIGGER || ti.lParam == INDEX)
						PostMessage(hMainWnd, WM_COMMAND, IDM_DELETE, 0);
				}

				if (pKd->wVKey == 0x43 && GetKeyState(VK_CONTROL)) {// Ctrl + C
					TCHAR name16[256] = {0};
					TV_ITEM tv;
					tv.mask = TVIF_TEXT | TVIF_PARAM;
					tv.hItem = treeItems[0];
					tv.pszText = name16;
					tv.cchTextMax = 256;

					if(!TreeView_GetItem(hTreeWnd, &tv))
						return 0;

					if (tv.lParam == COLUMN)
						_tcstok(name16, TEXT(":"));

					if (tv.lParam == TABLE && HIWORD(GetKeyState(VK_SHIFT))) {
						TCHAR* ddl = getDDL(name16, tv.lParam, false);
						if (ddl != NULL) {
							utils::setClipboardText(ddl);
							delete [] ddl;
						}
					} else {
						utils::setClipboardText(name16);
					}
				}

				return 1;
			}

			if (pHdr->hwndFrom == hTreeWnd && pHdr->code == TVN_SELCHANGED)
				treeItems[0] = TreeView_GetSelection(hTreeWnd);

			if (pHdr->hwndFrom == hTreeWnd && pHdr->code == TVN_ITEMEXPANDING)
				TreeView_SelectItem(hTreeWnd, ((LPNMTREEVIEW)lParam)->itemNew.hItem);

			if (pHdr->hwndFrom == hTreeWnd && pHdr->code == TVN_BEGINLABELEDIT) {
				const NMTVDISPINFO * pMi = (LPNMTVDISPINFO)lParam;
				if (!TreeView_GetParent(hTreeWnd, pMi->item.hItem))
					return true;

				if (pMi->item.state & TVIS_CUT)
					return true;

				HWND hEdit = TreeView_GetEditControl(hTreeWnd);
				cbOldTreeItemEdit = (WNDPROC) SetWindowLongPtr(hEdit, GWL_WNDPROC, (LONG_PTR)&cbNewTreeItemEdit);

				_tcscpy(treeEditName, pMi->item.pszText);
				SetWindowText(hEdit, _tcstok(pMi->item.pszText, TEXT(":")));
			}

			if (pHdr->hwndFrom == hTreeWnd && pHdr->code == TVN_ENDLABELEDIT) {
				NMTVDISPINFO * pMi = (LPNMTVDISPINFO)lParam;
				int type = pMi->item.lParam;

				if (pMi->item.pszText == NULL || _tcslen(pMi->item.pszText) == 0)
					return false;

				TCHAR query16[32000];
				if (type == COLUMN) {
					TCHAR tblname16[255];
					HTREEITEM hParent = TreeView_GetParent(hTreeWnd, treeItems[0]);
					TVITEM ti;
					ti.hItem = hParent;
					ti.mask = TVIF_PARAM | TVIF_TEXT;
					ti.pszText = tblname16;
					ti.cchTextMax = 255;
					TreeView_GetItem(hTreeWnd, &ti);
					if (ti.lParam != TABLE)
						return 0;

					TCHAR* colDesc16 = _tcstok(treeEditName, TEXT(":"));
					colDesc16 = colDesc16 ? _tcstok (NULL, TEXT(":")) : 0;

					_stprintf(query16, TEXT("alter table \"%ls\" rename column \"%ls\" to \"%ls\""), tblname16, treeEditName, pMi->item.pszText);
					if (_tcscmp(treeEditName, pMi->item.pszText)) {
						char* query8 = utils::utf16to8(query16);
						int rc = sqlite3_exec(db, query8, NULL, 0 , 0);
						delete [] query8;

						if (SQLITE_OK != rc) {
							showDbError(hMainWnd);
							return false;
						}
					}

					TCHAR item16[300] = {0};
					_stprintf(item16, TEXT("%ls:%ls"), pMi->item.pszText, colDesc16);
					pMi->item.pszText = item16;
					pMi->item.cchTextMax = _tcslen(item16) + 1;
					return true;
				}

				if (type == TABLE) {
					_stprintf(query16, TEXT("alter table \"%ls\" rename to \"%ls\""), treeEditName, pMi->item.pszText);
				} else if (type > 1) {
					const TCHAR* ddl = getDDL(treeEditName, pMi->item.lParam);
					if (!ddl)
						return 0;

					TCHAR* newDdl = utils::replace(ddl, treeEditName, pMi->item.pszText, _tcslen(TEXT("create ")) + _tcslen(TYPES16u[type]));
					_stprintf(query16, TEXT("drop %ls \"%ls\"; %ls;"), TYPES16[type], treeEditName, newDdl);
					delete [] ddl;
					delete [] newDdl;
				}

				if (!_tcscmp(treeEditName, pMi->item.pszText))
					return false;

				char* query8 = utils::utf16to8(query16);
				int rc = SQLITE_OK == sqlite3_exec(db, query8, NULL, 0 , 0);
				if (!rc)
					showDbError(hMainWnd);
				delete [] query8;
				return rc;
			}

			if (pHdr->hwndFrom == hTreeWnd && pHdr->code == (UINT)TVN_BEGINDRAG) {
				NMTREEVIEW* lpnmtv = (LPNMTREEVIEW)lParam;

				TCHAR buf[255];
				TV_ITEM tv{0};
				tv.mask = TVIF_TEXT | TVIF_PARAM;
				tv.pszText = buf;
				tv.cchTextMax = 255;
				tv.hItem = lpnmtv->itemNew.hItem;
				TreeView_GetItem(hTreeWnd, &tv);

				int len = _tcslen(buf);
				_tcstok(buf, TEXT(":"));

				if (tv.lParam < 0)
					return true;

				RECT rc{0};
				*(HTREEITEM*)&rc = tv.hItem;
				SendMessage(hTreeWnd, TVM_GETITEMRECT, true, (LPARAM)&rc);

				TreeView_Select(hTreeWnd, tv.hItem, TVGN_CARET);
				SetCapture(hMainWnd);
				ShowCursor(false);

				hDragWnd = CreateWindowEx(WS_EX_TOPMOST, WC_STATIC, buf, WS_CHILD | SS_LEFT | WS_VISIBLE,
					rc.left, rc.top, (rc.right - rc.left) * _tcslen(buf) / len, rc.bottom - rc.top, hMainWnd, (HMENU)0, GetModuleHandle(0), NULL);;
				SetWindowLong(hDragWnd, GWL_USERDATA, tv.lParam);
				setTreeFont(hDragWnd);
				InvalidateRect(hDragWnd, NULL, true);
			}

			if (pHdr->hwndFrom == hTreeWnd && pHdr->code == (UINT)NM_CUSTOMDRAW) {
				LPNMTVCUSTOMDRAW pCustomDraw = (LPNMTVCUSTOMDRAW)lParam;
				if (pCustomDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
					return CDRF_NOTIFYITEMDRAW;

				if (pCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
					BOOL isCut = TreeView_GetItemState(pHdr->hwndFrom, (HTREEITEM)pCustomDraw->nmcd.dwItemSpec, TVIS_CUT) & TVIS_CUT;
					pCustomDraw->clrText = pCustomDraw->clrTextBk != RGB(255, 255, 255) ? RGB(255, 255, 255) :
						isCut ? RGB(200, 200, 200) : RGB(0, 0, 0);
				}

				return CDRF_DODEFAULT;
			}

			if (pHdr->code == TTN_GETDISPINFO) {
				LPTOOLTIPTEXT pTtt = (LPTOOLTIPTEXT) lParam;
				bool isToolTip = pTtt->hdr.hwndFrom == (HWND)SendMessage(hToolbarWnd, TB_GETTOOLTIPS, 0, 0);
				bool isTabTip = pTtt->hdr.hwndFrom == (HWND)SendMessage(hTabWnd, TCM_GETTOOLTIPS, 0, 0) && (pTtt->hdr.idFrom < MAX_RESULT_COUNT);
				if (isToolTip || isTabTip) {
					SendMessage(pTtt->hdr.hwndFrom, TTM_SETMAXTIPWIDTH, 0, MAX_TEXT_LENGTH);
					if (isTabTip) {
						int tabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
						pTtt->lpszText = tabs[tabNo].tabTooltips[pTtt->hdr.idFrom];
					}
					else {
						pTtt->hinst = GetModuleHandle(0);
						pTtt->lpszText = (LPWSTR)pTtt->hdr.idFrom;
					}
				}
			}

			if (pHdr->hwndFrom == hToolbarWnd && pHdr->code == (UINT)NM_CUSTOMDRAW) {
				NMTBCUSTOMDRAW *pCustomDraw = (NMTBCUSTOMDRAW*)lParam;
				if (pCustomDraw->nmcd.dwDrawStage == CDDS_PREERASE) {
					HDC hdc = pCustomDraw->nmcd.hdc;
					RECT rect;
					GetClientRect(hToolbarWnd, &rect);
					FillRect(hdc, &rect, GetSysColorBrush(COLOR_3DFACE));
					return CDRF_SKIPDEFAULT ;
				}
			}

			if (pHdr->code == (DWORD)NM_RCLICK && !_tcscmp(wndClass, WC_LISTVIEW)) {
				NMITEMACTIVATE* ia = (LPNMITEMACTIVATE) lParam;
				currCell = {ia->hdr.hwndFrom, ia->iItem, ia->iSubItem};

				POINT p;
				GetCursorPos(&p);

				HMENU hCompareMenu = GetSubMenu(hResultMenu, 9);
				int cnt = GetMenuItemCount(hCompareMenu);
				for (int i = 0; i < cnt; i++)
					DeleteMenu(hCompareMenu, 0, MF_BYPOSITION);

				int currColCount = ListView_GetColumnCount(ia->hdr.hwndFrom);
				int menuNo = 0;

				for (int tabNo = 0; tabNo < MAX_TAB_COUNT && menuNo < MAX_COMPARE_RESULT; tabNo++) {
					TEditorTab tab = tabs[tabNo];
					if (!tab.hEditorWnd)
						continue;

					TCHAR tabname16[80] = {0};
					SendMessage(hMainTabWnd, WMU_TAB_GET_TEXT, tabNo, (LPARAM)tabname16);

					for (int resultNo = 0; resultNo < TabCtrl_GetItemCount(tab.hTabWnd) && menuNo < MAX_COMPARE_RESULT; resultNo++) {
						HWND hListWnd = GetDlgItem(tab.hTabWnd, IDC_TAB_ROWS + resultNo);
						if (!hListWnd)
							continue;

						if (hListWnd == ia->hdr.hwndFrom)
							continue;

						if (currColCount != ListView_GetColumnCount(hListWnd))
							continue;

						TCHAR resname16[80];
						TabCtrl_GetItemText(tab.hTabWnd, resultNo, resname16, 80);

						TCHAR menu16[160];
						_stprintf(menu16, TEXT("%ls > %ls"), tabname16, resname16);

						AppendMenu(hCompareMenu, MF_STRING, IDM_RESULT_COMPARE + menuNo, menu16);

						MENUITEMINFO mii{0};
						mii.cbSize = sizeof(MENUITEMINFO);
						mii.fMask = MIIM_DATA;
						mii.dwItemData = MAKELPARAM(tabNo, resultNo);
						SetMenuItemInfo(hCompareMenu, IDM_RESULT_COMPARE + menuNo, false, &mii);
						menuNo++;
					}
				}

				if (GetMenuItemCount(hCompareMenu) == 0)
					AppendMenu(hCompareMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, TEXT("No similar results"));

				TrackPopupMenu(hResultMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN, p.x, p.y, 0, hMainWnd, NULL);
			}

			if (pHdr->code == EN_MSGFILTER && wParam == IDC_EDITOR) {
				return processEditorEvents(pF);
			}

			if (pHdr->code == LVN_COLUMNCLICK) {
				NMLISTVIEW* pLV = (NMLISTVIEW*)lParam;
				return ListView_Sort(pHdr->hwndFrom, pLV->iSubItem);
			}

			if (pHdr->code == NM_TAB_ADD && pHdr->hwndFrom == hMainTabWnd) {
				ShowWindow(hEditorWnd, SW_HIDE);
				ShowWindow(hTabWnd, SW_HIDE);
				int tabNo = wParam;
				int id = 0;
				for (int i = 0; i < tabNo; i++)
					id = id < tabs[i].id ? tabs[i].id : id;

				hEditorWnd = CreateWindowEx(0, TEXT("RICHEDIT50W"), NULL, WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | ES_NOHIDESEL, 100, 0, 100, 100, hMainWnd, (HMENU)IDC_EDITOR, GetModuleHandle(0),  NULL);
				SendMessage(hEditorWnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_KEYEVENTS | ENM_MOUSEEVENTS | ENM_DROPFILES);
				SendMessage(hEditorWnd, EM_EXLIMITTEXT, 0, 32767 * 10);
				if (prefs::get("word-wrap"))
					toggleWordWrap(hEditorWnd);
				hTabWnd = CreateWindow(WC_TABCONTROL, NULL, WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | TCS_TOOLTIPS, 100, 100, 100, 100, hMainWnd, (HMENU)IDC_TAB, GetModuleHandle(0), NULL);
				cbOldResultTab = (WNDPROC)SetWindowLong(hTabWnd, GWL_WNDPROC, (LONG)cbNewResultTab);
				DragAcceptFiles(hEditorWnd, true);

				tabs[tabNo].hEditorWnd = hEditorWnd;
				tabs[tabNo].hTabWnd = hTabWnd;
				tabs[tabNo].hQueryListWnd = CreateWindow(WC_LISTBOX, NULL, WS_CHILD | SS_LEFT | WS_BORDER, 20, 20, 100, 100, hEditorWnd, (HMENU)IDC_QUERYLIST, GetModuleHandle(0), NULL);
				tabs[tabNo].id = id + 1;

				SendMessage(hEditorWnd, EM_SETWORDWRAPMODE, WBF_WORDWRAP, 0);
				SendMessage(hTabWnd, WM_SETFONT, (LPARAM)hDefFont, true);
				SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, tabNo, 0);
			}

			if (pHdr->code == NM_TAB_CHANGE && pHdr->hwndFrom == hMainTabWnd) {
				int tabNo = wParam;
				ShowWindow(hEditorWnd, SW_HIDE);
				ShowWindow(hTabWnd, SW_HIDE);
				ShowWindow(cli.hResultWnd, tabNo == -1 ? SW_SHOW : SW_HIDE);

				hEditorWnd = tabNo != -1 ? tabs[tabNo].hEditorWnd : cli.hEditorWnd;
				hTabWnd = tabNo != - 1 ? tabs[tabNo].hTabWnd : 0;
				SendMessage(hStatusWnd, SB_SETTEXT, SB_ELAPSED_TIME, tabNo != -1 ? (LPARAM)tabs[tabNo].queryElapsedTimes[TabCtrl_GetCurSel(hTabWnd)] : 0);
				SendMessage(hMainWnd, WMU_UPDATE_ROWNO, 0, 0);

				ShowWindow(hEditorWnd, SW_SHOW);
				ShowWindow(hTabWnd, SW_SHOW);
				setEditorFont(hEditorWnd);
				SetFocus(hEditorWnd);
				updateSizes(true);

				bool isRunning = tabNo >=0 ? tabs[tabNo].thread : cli.thread;
				updateExecuteMenu(!isRunning && db != NULL);
				Toolbar_SetButtonState(hToolbarWnd, IDM_INTERRUPT, isRunning ? TBSTATE_ENABLED : TBSTATE_HIDDEN);
				updateTransactionState();
			}

			if (pHdr->code == NM_TAB_DELETE && pHdr->hwndFrom == hMainTabWnd) {
				int tabNo = wParam;
				TEditorTab* tab = &tabs[tabNo];
				DestroyWindow(tab->hEditorWnd);
				DestroyWindow(tab->hTabWnd);
				DestroyWindow(tab->hQueryListWnd);
				sqlite3_close(tab->db);

				for (int i = tabNo; i < MAX_TAB_COUNT - 1; i++)
					tabs[i] = tabs[i + 1];

				tabs[MAX_TAB_COUNT - 2]	= {0};

				tabs[SendMessage(hMainTabWnd, WMU_TAB_GET_COUNT, 0, 0)] = {0};
			}

			if (pHdr->code == NM_TAB_REQUEST_DELETE && pHdr->hwndFrom == hMainTabWnd) {
				TBBUTTONINFO tbi{0};
				tbi.cbSize = sizeof(TBBUTTONINFO);
				tbi.dwMask = TBIF_LPARAM;
				SendMessage(hToolbarWnd, TB_GETBUTTONINFO, IDM_INTERRUPT, (LPARAM)&tbi);
				bool isRunning = tabs[wParam].thread != 0;
				if (isRunning)
					MessageBox(hMainWnd, TEXT("The tab is busy"), TEXT("Info"), MB_OK);
				return isRunning;
			}
		}
		break;

		case WM_PARENTNOTIFY: {
			if (LOWORD(wParam) == WM_LBUTTONDOWN && IsWindowVisible(hAutoComplete) && (GetFocus() != hAutoComplete))
				ShowWindow(hAutoComplete, SW_HIDE);
		}
		break;

		case WMU_HIGHLIGHT: {
			processHighlight(hEditorWnd, isRequireHighligth, isRequireParenthesisHighligth, isRequireOccurrenceHighlight);
			isRequireHighligth = false;
			isRequireParenthesisHighligth = false;

			int rc = SendMessage(hEditorWnd, EM_GETSEL, 0, 0);
			isRequireOccurrenceHighlight = LOWORD(rc) != HIWORD(rc);
		}
		break;

		// wParam = 1 om Ctrl + Space
		case WMU_SHOW_INFO: {
			TCHAR result16[MAX_TEXT_LENGTH]{0};
			hideTooltip();

			bool isHint = wParam != 0;
			TCHAR* text = getCurrentText(hEditorWnd);

			// Is keyword/function?
			if (_tcslen(text) && _tcslen(result16) == 0) {
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select brief || char(10) || description || char(10) || char(10) || example from help where (word = lower(?1) or alt = lower(?1))", -1, &stmt, 0)) {
					char* word8 = utils::utf16to8(text);
					sqlite3_bind_text(stmt, 1, word8, strlen(word8), SQLITE_TRANSIENT);
					delete [] word8;

					if (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* res16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
						_tcsncpy(result16, res16, MAX_TEXT_LENGTH);
						delete [] res16;
					}
				}
				sqlite3_finalize(stmt);
			}

			// Is table?
			if (_tcslen(text) && _tcslen(result16) == 0) {
				sqlite3_stmt *stmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db,
						isHint ?
							"select upper(?1) || char(10) || group_concat(name || ': ' || type || iif(pk,' [pk]',''),'\n') from pragma_table_xinfo where schema = ?2 and arg = ?1" :
							"select count(*) from pragma_table_info where schema = ?2 and arg = ?1 limit 1", -1, &stmt, 0)) {
					TCHAR* tablename16 = utils::getName(text, false);
					TCHAR* schema16 = utils::getName(text, true);
					char* tablename8 = utils::utf16to8(tablename16);
					char* schema8 = utils::utf16to8(schema16);

					sqlite3_bind_text(stmt, 1, tablename8, strlen(tablename8), SQLITE_TRANSIENT);
					sqlite3_bind_text(stmt, 2, schema8, strlen(schema8), SQLITE_TRANSIENT);

					delete [] tablename8;
					delete [] schema8;
					delete [] tablename16;
					delete [] schema16;

					if (SQLITE_ROW == sqlite3_step(stmt)) {
						if (isHint) {
							TCHAR* res16 = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
							_tcsncpy(result16, res16, MAX_TEXT_LENGTH);
							delete [] res16;
						}

						if (!isHint && sqlite3_column_int(stmt, 0) > 0) {
							result16[0] = 0;
							_sntprintf(editTableData16, 255, TEXT("%ls"), text);
							openDialog(IDD_EDITDATA, dialogs::cbDlgEditData);
						}
					}
				}
				sqlite3_finalize(stmt);
			}

			// Is function argument?
			if (isHint && _tcslen(result16) == 0) {
				int crStart, crEnd;
				SendMessage(hEditorWnd, EM_GETSEL, (WPARAM)&crStart, (WPARAM)&crEnd);
				SetWindowRedraw(hEditorWnd, false);

				bool isSearch = true;
				int pCounter = 1;
				int aCounter = 0;
				int crLineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, -1, 0);
				int currLineNo = crLineNo;
				while (isSearch && currLineNo >= 0) {
					int currLineIdx = SendMessage(hEditorWnd, EM_LINEINDEX, currLineNo, 0);
					int currLineSize = SendMessage(hEditorWnd, EM_LINELENGTH, currLineIdx, 0);
					TCHAR currLine[currLineSize + 1]{0};
					currLine[0] = currLineSize;
					SendMessage(hEditorWnd, EM_GETLINE, currLineNo, (LPARAM)currLine);

					int pos = currLineNo == crLineNo ? crStart - currLineIdx - 1: currLineSize - 1;
					while (isSearch && pos >= 0) {
						TCHAR c = currLine[pos];

						if (_tcschr(TEXT(",();"), c) != 0 && !RichEdit_GetTextColor(hEditorWnd, currLineIdx + pos)) {
							if (c == TEXT(',') && pCounter == 1)
								aCounter++;

							if (c == TEXT(')'))
								pCounter++;

							if (c == TEXT('(')) {
								pCounter--;
								if (pCounter == 0)
									isSearch = false;

								if (pCounter == 0 && pos > 0) {
									int cCounter = 0;
									pos--;
									int posStart = pos, posEnd = pos;
									while (pos >= 0) {
										bool isChar = _istalnum(currLine[pos]) || (currLine[pos] == TEXT('_'));
										cCounter += isChar;

										if (isChar && cCounter == 1)
											posEnd = pos;

										if ((!isChar || pos == 0) && cCounter > 0) {
											posStart = pos + (pos > 0);
											break;
										}

										pos--;
									}

									if (posStart < posEnd) {
										TCHAR fname16[posEnd - posStart + 2]{0};
										_tcsncpy(fname16, currLine + posStart, posEnd - posStart + 1);

										sqlite3_stmt *stmt;
										char query8[] = "with " \
											"t (fname, args, nargs) as (select coalesce(max(brief), ?1), max(args), max(nargs) from help where word = ?1), " \
											"split(arg, str, no) as ( " \
											"select '', args || '|', 0 from t union all " \
											"select substr(str, 0, instr(str, '|')), substr(str, instr(str, '|') + 1), no + 1 from split where str <> '')," \
											"t2(arg) as (select coalesce(max(arg), (?2 + 1) || ' argument') from split where no = ?2 + 1)"
											"select case when nargs = -1 or ?2 < nargs or nargs is null then (select arg from t2) when nargs = 0 then 'The function takes no arguments' else 'The maximum arguments count is exceeded by ' || (?2 + 1) || ' argument' end " \
											"|| char(10, 10) || fname from t";

										if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, query8, -1, &stmt, 0)) {
											char* fname8 = utils::utf16to8(fname16);
											sqlite3_bind_text(stmt, 1, fname8, strlen(fname8), SQLITE_TRANSIENT);
											sqlite3_bind_int(stmt, 2, aCounter);
											delete [] fname8;

											if (SQLITE_ROW == sqlite3_step(stmt)) {
												TCHAR* res16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
												_tcscpy(result16, res16);
												delete [] res16;
											}
										}

										sqlite3_finalize(stmt);
									}
								}
							}

							if (c == TEXT(';'))
								isSearch = false;
						}

						pos--;

					}
					currLineNo--;
				}

				SendMessage(hEditorWnd, EM_SETSEL, (WPARAM)crStart, (WPARAM)crEnd);
				SetWindowRedraw(hEditorWnd, true);
			}

			delete [] text;

			if (_tcslen(result16) > 0) {
				POINT p{0};
				GetCaretPos(&p);
				ClientToScreen(hEditorWnd, &p);

				showTooltip(p.x, p.y + 20, result16);
				ShowWindow(hAutoComplete, SW_HIDE);
			}

			return true;
		}
		break;

		case WMU_APPEND_TEXT: {
			TCHAR* buf = (TCHAR*)wParam;
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
		}
		break;

		case WMU_OPEN_NEW_TAB: {
			int tabNo = SendMessage(hMainTabWnd, WMU_TAB_ADD, 0, 0);
			SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, tabNo, 0);
		}
		break;

		default:
			return DefWindowProc (hWnd, msg, wParam, lParam);
	}

	return 0;
}


LRESULT CALLBACK cbNewListView(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// https://devblogs.microsoft.com/oldnewthing/20070626-00/?p=26263
	if (msg == WM_GETDLGCODE && lParam && ((MSG*)lParam)->message == WM_KEYDOWN && (wParam == VK_RETURN))
	   return DLGC_WANTMESSAGE;

	// Quick reference tooltip
	if (msg == WM_SYSKEYDOWN && wParam == VK_MENU)
		return 1;

	if (msg == WM_SYSKEYUP && wParam == VK_MENU && IsWindowVisible(hTooltipWnd))
		hideTooltip();

	if (msg == WM_HSCROLL && LOWORD(wParam) == SB_ENDSCROLL)
		InvalidateRect(hWnd, 0, false);

	if (msg == WM_DESTROY) {
		byte* datatypes = (byte*)GetProp(hWnd, TEXT("DATATYPES"));
		if (datatypes)
			delete [] datatypes;
	}

	if(msg == WM_NOTIFY) {
		NMHDR* pHdr = (LPNMHDR)lParam;
		// Prevent zero-width column resizing
		if ((pHdr->code == HDN_BEGINTRACK || pHdr->code == HDN_DIVIDERDBLCLICK) && ListView_GetColumnWidth(hWnd, ((LPNMHEADER)lParam)->iItem) == 0)
			return true;

		// issue #68: prevent sizing column smaller than caption
		if (pHdr->code == HDN_ITEMCHANGED && pHdr->hwndFrom == ListView_GetHeader(hWnd) && (GetTickCount() - doubleClickTick < 200))
			InvalidateRect(pHdr->hwndFrom, NULL, false);

		if (pHdr->code == HDN_ITEMCHANGING && pHdr->hwndFrom == ListView_GetHeader(hWnd) && (GetTickCount() - doubleClickTick < 100)) {
			NMHEADER* nmh = (LPNMHEADER) lParam;
			if (nmh->pitem && nmh->pitem->mask & HDI_WIDTH) {
				HWND hHeader = pHdr->hwndFrom;
				int w = ListView_GetColumnWidth(hWnd, nmh->iItem);
				if (nmh->pitem->cxy == w)
					return false;

				TCHAR colname16[256];
				Header_GetItemText(hHeader, nmh->iItem, colname16, 255);
				HDC hDC = GetDC(hHeader);
				SIZE s{0};
				HFONT hOldFont = (HFONT)SelectObject(hDC, (HFONT)SendMessage(hHeader, WM_GETFONT, 0, 0));
				GetTextExtentPoint32(hDC, colname16, _tcslen(colname16), &s);
				SelectObject(hDC, hOldFont);
				ReleaseDC(pHdr->hwndFrom, hDC);

				nmh->pitem->cxy = MAX(s.cx + 12, nmh->pitem->cxy);
			}
		}

		if (pHdr->code == HDN_DIVIDERDBLCLICK && pHdr->hwndFrom == ListView_GetHeader(hWnd))
			doubleClickTick = GetTickCount();
		/* issue #68 end */
	}

	// Edit data
	if (msg == WM_COMMAND && HIWORD(wParam) == CBN_KILLFOCUS && LOWORD(wParam) == IDC_DLG_VALUE_SELECTOR)
		DestroyWindow((HWND)lParam);

	return CallWindowProc(cbOldListView, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK cbNewEdit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (processEditKeys(hWnd, msg, wParam, lParam))
		return 0;

	return CallWindowProc(cbOldEdit, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK cbNewTreeItemEdit(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_GETDLGCODE)
		return (DLGC_WANTALLKEYS | CallWindowProc(cbOldTreeItemEdit, hWnd, msg, wParam, lParam));

	if ((msg == WM_KEYUP) && (wParam == VK_ESCAPE)) {
		DestroyWindow(hWnd);
		return 0;
	}

	if (processEditKeys(hWnd, msg, wParam, lParam))
		return 0;

	return CallWindowProc(cbOldTreeItemEdit, hWnd, msg, wParam, lParam);
}

int CALLBACK cbListComparator(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
	int colNo = LOWORD(lParamSort);
	int order = HIWORD(lParamSort);

	TCHAR buf[256]{0};
	TCHAR buf2[256]{0};

	ListView_GetItemText(hSortingResultWnd, lParam1, colNo, buf, 255);
	ListView_GetItemText(hSortingResultWnd, lParam2, colNo, buf2, 255);

	double num, num2;
	bool isNum = utils::isNumber(buf, &num) && utils::isNumber(buf2, &num2);

	return (order ? 1 : -1) * (isNum ? (num > num2 ? 1 : -1) : _tcscoll(buf, buf2));
}

unsigned int __stdcall processCliQuery (void* data) {
	updateExecuteMenu(false);
	Toolbar_SetButtonState(hToolbarWnd, IDM_INTERRUPT, TBSTATE_ENABLED);
	SendMessage(hMainTabWnd, WMU_TAB_SET_STYLE, -1, 1);

	float elapsed = 0;
	if (!cli.db)
		openConnection(&cli.db, sqlite3_db_filename(db, 0));

	int size = GetWindowTextLength(cli.hEditorWnd);
	TCHAR sql16[size + 1] = {0};
	GetWindowText(cli.hEditorWnd, sql16, size + 1);
	char *sql8 = utils::utf16to8(sql16);

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(cli.db, sql8, -1, &stmt, 0);
	if (rc == SQLITE_OK && cli.isPlan && !sqlite3_stmt_isexplain(stmt)) {
		sqlite3_finalize(stmt);
		char* new8 = new char[strlen(sql8) + strlen("explain query plan ") + 1]{0};
		sprintf(new8, "explain query plan %s", sql8);
		delete [] sql8;
		sql8 = new8;
		rc = sqlite3_prepare_v2(cli.db, sql8, -1, &stmt, 0);
	}

	if (sqlite3_bind_parameter_count(stmt))
		DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_BIND_PARAMETERS), hMainWnd, (DLGPROC)&dialogs::cbDlgBindParameters, (LPARAM)stmt);

	TCHAR result16[MAX_TEXT_LENGTH]{0};
	if (sqlite3_bind_parameter_count(stmt)) {
		_tcscat(result16, TEXT("EXECUTE: "));
		TCHAR* esql16 = utils::utf8to16(sqlite3_expanded_sql(stmt));
		_tcscat(result16, esql16);
		_tcscat(result16, TEXT("\n"));
		delete [] esql16;
	}

	if (rc == SQLITE_OK) {
		DWORD tStart = GetTickCount();
		rc = sqlite3_step(stmt);
		int rowCount = 0;
		int colCount = sqlite3_column_count(stmt);

		if (colCount && (rc == SQLITE_ROW || rc == SQLITE_DONE || rc == SQLITE_OK)) {
			HWND hListWnd = GetDlgItem(cli.hResultWnd, IDC_CLI_RAWDATA);
			ListBox_ResetContent(hListWnd);

			size_t maxWidths[colCount]{0};

			for (int i = 0; i < colCount; i++) {
				TCHAR* name16 = utils::utf8to16(sqlite3_column_name(stmt, i));
				ListBox_AddString(hListWnd, name16);
				maxWidths[i] = _tcslen(name16);
			}

			int rowLimit = prefs::get("cli-row-limit");
			while (rc == SQLITE_ROW && (rowCount < rowLimit || rowLimit <= 0)) {
				for (int i = 0; i < colCount; i++) {
					if (sqlite3_column_type(stmt, i) != SQLITE_BLOB) {
						TCHAR* value16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, i));
						TCHAR* tvalue16 = utils::replaceAll(value16, TEXT("\r\n"), TEXT(" "));
						TCHAR trimmed16[256]{0};
						_tcsncpy(trimmed16, tvalue16, 255);
						delete [] tvalue16;
						delete [] value16;
						if (maxWidths[i] < _tcslen(trimmed16))
							maxWidths[i] = _tcslen(trimmed16);

						ListBox_AddString(hListWnd, trimmed16);
					} else {
						if (maxWidths[i] < 6)
							maxWidths[i] = 6;
						ListBox_AddString(hListWnd, TEXT("(BLOB)"));
					}
				}

				rowCount++;
				rc = sqlite3_step(stmt);
			}

			int w = (colCount - 1) * 3;
			for (int i = 0; i < colCount; i++)
				w += maxWidths[i];

			TCHAR line16[w + 1]{0};
			for (int i = 0; i < w; i++)
				line16[i] = TEXT('-');
			_tcscat(result16, line16);
			_tcscat(result16, TEXT("\n"));

			for (int i = 0; i < colCount; i++) {
				TCHAR name16[255]{0};
				ListBox_GetText(hListWnd, i, name16);
				TCHAR buf16[maxWidths[i] + 10]{0};
				_stprintf(buf16, TEXT("%-*s"), maxWidths[i], name16);
				_tcscat(result16, buf16);
				_tcscat(result16, (i < colCount - 1) ? TEXT(" | ") : TEXT("\n"));
			}
			_tcscat(result16, line16);
			_tcscat(result16, TEXT("\n"));

			for (int rowNo = 0; rowNo < rowCount; rowNo++) {
				for (int i = 0; i < colCount; i++) {
					TCHAR value16[maxWidths[i] + 1]{0};
					ListBox_GetText(hListWnd, i + (rowNo + 1) * colCount, value16);
					TCHAR buf16[maxWidths[i] + 10]{0};
					_stprintf(buf16, TEXT("%-*s"), maxWidths[i], value16);
					_tcscat(result16, buf16);
					_tcscat(result16, (i < colCount - 1) ? TEXT(" | ") : TEXT("\n"));
				}
			}

			if (rc == SQLITE_ROW) {
				_tcscat(result16, TEXT("... more rows available\n"));
				rc = SQLITE_DONE;
			}

			if (rowCount)
				_tcscat(result16, line16);

			DestroyWindow(hListWnd);
		}

		if (rc == SQLITE_DONE) {
			TCHAR total16[255];
			elapsed = (GetTickCount() - tStart) / 1000.0;
			_stprintf(total16, TEXT("%lsDone. Elapsed time: %.2fs"), colCount > 0 ? TEXT("\n") : TEXT(""), elapsed);
			_tcscat(result16, total16);
		}

		rc = SQLITE_OK;
	}
	sqlite3_finalize(stmt);

	if (rc != SQLITE_OK) {
		const char* msg8 = sqlite3_errmsg(db);
		TCHAR* msg16 = utils::utf8to16(msg8);
		_stprintf(result16, TEXT("Error: %ls"), msg16);
		delete [] msg16;
	}

	if (_tcslen(result16)) {
		_tcscat(result16, TEXT("\n\n============================================================\n\n"));
		SendMessage(cli.hResultWnd, EM_SETSEL, 0, 0);
		SendMessage(cli.hResultWnd, EM_REPLACESEL, 0, (LPARAM)result16);

		if (rc == SQLITE_OK) {
			SendMessage(cli.hResultWnd, EM_SETSEL, 0, 0);
			SendMessage(cli.hResultWnd, EM_REPLACESEL, 0, (LPARAM)TEXT("\n"));
			SendMessage(cli.hResultWnd, EM_SETSEL, 0, 0);
			SendMessage(cli.hResultWnd, EM_REPLACESEL, 0, (LPARAM)sql16);
			SetWindowText(cli.hEditorWnd, 0);

			if(SQLITE_OK == sqlite3_prepare_v2(prefs::db, "insert into cli (time, dbname, query, elapsed, result) values (strftime('%s', 'now'), ?1, ?2, ?3, ?4)", -1, &stmt, 0)) {
				char* dbname8 = utils::getFileName(sqlite3_db_filename(db, 0));
				sqlite3_bind_text(stmt, 1, dbname8, strlen(dbname8), SQLITE_TRANSIENT);
				delete [] dbname8;

				char* sql8 = utils::utf16to8(sql16);
				sqlite3_bind_text(stmt, 2, sql8, strlen(sql8), SQLITE_TRANSIENT);
				delete [] sql8;

				sqlite3_bind_int(stmt, 3, elapsed);

				char* result8 = utils::utf16to8(result16);
				sqlite3_bind_text(stmt, 4, result8, strlen(result8), SQLITE_TRANSIENT);
				delete [] result8;

				if (SQLITE_DONE != sqlite3_step(stmt))
					showDbError(hMainWnd);
			}
			sqlite3_finalize(stmt);
		}
	}

	delete [] sql8;

	RedrawWindow(cli.hResultWnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME);
	SetWindowLong(cli.hEditorWnd, GWL_USERDATA, 0);

	CloseHandle(cli.thread);
	cli.thread = 0;

	updateExecuteMenu(true);
	Toolbar_SetButtonState(hToolbarWnd, IDM_INTERRUPT, TBSTATE_HIDDEN);
	SendMessage(hMainTabWnd, WMU_TAB_SET_STYLE, -1, 0);
	updateTransactionState();
	return 1;
}

int executeCLIQuery(bool isPlan) {
	int size = GetWindowTextLength(cli.hEditorWnd);
	if (size == 0 || cli.thread)
		return 0;

	setEditorColor(cli.hResultWnd, RGB(0, 196, 0), true);

	TCHAR sql16[size + 1] = {0};
	GetWindowText(cli.hEditorWnd, sql16, size + 1);
	if (sql16[0] == TEXT('.')) {
		if (_tcscmp(sql16, TEXT(".clear")) == 0) {
			SetWindowText(cli.hResultWnd, NULL);
		} else if (_tcsstr(sql16, TEXT(".last ")) != NULL) {
			int cnt = _ttoi(sql16 + 6);
			SetWindowText(cli.hResultWnd, NULL);
			loadCLIResults(cnt);
		} else if (_tcsstr(sql16, TEXT(".set ")) == sql16) {
			TCHAR result16[MAX_TEXT_LENGTH];
			_stprintf(result16, TEXT("%ls\n\n"), sql16);

			int len = _tcslen(sql16);
			if (len > 5) {
				int npos = 5;
				while (npos < len && sql16[npos] == TEXT(' '))
					npos++;

				int vpos = npos;
				while (vpos < len && sql16[vpos] != TEXT(' '))
					vpos++;
				vpos++;

				TCHAR name16[len]{0}, value16[len]{0};
				_tcsncpy(name16, sql16 + npos, vpos - npos - 1);
				_tcsncpy(value16, sql16 + vpos, len - vpos + 1);

				char* name8 = utils::utf16to8(name16);
				char* value8 = utils::utf16to8(value16);

				_stprintf(result16, prefs::set(name8, _ttoi(value16)) || prefs::set(name8, value8, true) ? TEXT("%ls\n\nDone: %ls = %ls") : TEXT("%ls\n\nError: invalid name %ls for value %ls"), sql16, name16, value16);

				delete [] name8;
				delete [] value8;
			} else {
				_tcscat(result16, TEXT("Usage: .set <name> <value>"));
			}

			_tcscat(result16, TEXT("\n\n============================================================\n\n"));
			SendMessage(cli.hResultWnd, EM_SETSEL, 0, 0);
			SendMessage(cli.hResultWnd, EM_REPLACESEL, 0, (LPARAM)result16);
		} else if (_tcsstr(sql16, TEXT(".get ")) == sql16 || _tcscmp(sql16, TEXT(".get")) == 0) {
			TCHAR result16[MAX_TEXT_LENGTH];
			_stprintf(result16, TEXT("%ls\n\n"), sql16);

			sqlite3_stmt* stmt;
			if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select name, value from prefs where name = coalesce(trim(?1), name) and name not like 'editor-%' order by 1", -1, &stmt, 0)) {
				if (_tcslen(sql16) > 5) {
					char* name8 = utils::utf16to8(sql16 + 5);
					sqlite3_bind_text(stmt, 1, name8, strlen(name8), SQLITE_TRANSIENT);
					delete [] name8;
				}

				while(SQLITE_ROW == sqlite3_step(stmt)) {
					const char* name8 = (const char*)sqlite3_column_text(stmt, 0);
					const char* value8 = (const char*)sqlite3_column_text(stmt, 1);
					int value = prefs::get(name8);
					char buf8[strlen(name8) + strlen(value8) + 10];
					if (value == -1)
						sprintf(buf8, "%s: %s\n", name8, value8);
					else
						sprintf(buf8, "%s: %i\n", name8, value);
					TCHAR* buf16 = utils::utf8to16(buf8);
					_tcscat(result16, buf16);
					delete [] buf16;
				}
			}
			sqlite3_finalize(stmt);

			_tcscat(result16, TEXT("\n\n============================================================\n\n"));
			SendMessage(cli.hResultWnd, EM_SETSEL, 0, 0);
			SendMessage(cli.hResultWnd, EM_REPLACESEL, 0, (LPARAM)result16);
		} else {
			TCHAR buf[MAX_TEXT_LENGTH];
			LoadString(GetModuleHandle(NULL), IDS_CLI_HELP, buf, MAX_TEXT_LENGTH);
			_tcscat(buf, TEXT("\n\n============================================================\n\n"));
			SendMessage(cli.hResultWnd, EM_SETSEL, 0, 0);
			SendMessage(cli.hResultWnd, EM_REPLACESEL, 0, (LPARAM)buf);
		}

		SetWindowText(cli.hEditorWnd, NULL);
		SetFocus(cli.hEditorWnd);
		return 0;
	}

	cli.isPlan = isPlan;
	cli.thread = (HANDLE)_beginthreadex(0, 0, &processCliQuery, 0, 0, 0);
	return 1;
}

static int cbProcessEditorQueryBatch(void *queryNo, int count, char **data, char **columns) {
	(*(int*)queryNo)++;
	return 0;
}

unsigned int __stdcall processEditorQuery (void* data) {
	TEditorTab* tab = (TEditorTab*)data;
	updateExecuteMenu(false);
	Toolbar_SetButtonState(hToolbarWnd, IDM_INTERRUPT, TBSTATE_ENABLED);

	if (!tab->db)
		openConnection(&tab->db, sqlite3_db_filename(db, 0));

	// reset meta-data cache
	// If a table was deleted in an another tab then the table can still exist in this
	sqlite3_exec(tab->db, "select 1 from sqlite_master limit 0", 0, 0, 0);

	auto getTabNo = [](int id) {
		for (int i = 0; i < MAX_TAB_COUNT; i++)
			if (tabs[i].id == id)
				return i;

		return -1;
	};

	TEditorTab _tab = *tab;
	SendMessage(hMainTabWnd, WMU_TAB_SET_STYLE, getTabNo(_tab.id), 1);

	auto detectMetaChanges = [](char* query8) {
		strlwr(query8);
		return strstr(query8, "create table") || strstr(query8, "create virtual table") || strstr(query8, "create view") ||
			strstr(query8, "create index") || strstr(query8, "create trigger") ||
			strstr(query8, "drop table") || strstr(query8, "drop view") || strstr(query8, "alter table") ||
			strstr(query8, "drop index") || strstr(query8, "drop trigger");
	};
	bool isMetaChanged = false;

	if (_tab.isBatch) {
		TCITEM tci;
		tci.mask = TCIF_TEXT | TCIF_IMAGE;
		tci.iImage = -1;
		tci.pszText = TEXT("Batch");
		tci.cchTextMax = 6;
		TabCtrl_InsertItem(_tab.hTabWnd, 0, &tci);

		DWORD tBatchStart = GetTickCount();
		HWND hResultWnd = GetDlgItem(_tab.hTabWnd, IDC_TAB_MESSAGE);

		if (prefs::get("synchronous-off"))
			sqlite3_exec(_tab.db, "pragma synchronous = 0", NULL, 0, NULL);

		bool isTrn = sqlite3_get_autocommit(_tab.db);
		if (isTrn)
			sqlite3_exec(_tab.db, "begin", NULL, 0, NULL);

		int size = ListBox_GetTextLen(_tab.hQueryListWnd, 0) + 1;
		TCHAR query16[size]{0};
		ListBox_GetText(_tab.hQueryListWnd, 0, query16);

		char* query8 = utils::utf16to8(query16);
		char *err8 = NULL;
		int queryNo = 0;
		if (SQLITE_OK != sqlite3_exec(_tab.db, query8, cbProcessEditorQueryBatch, &queryNo, &err8)) {
			char* qstart8 = query8;
			while (queryNo > 0) {
				const char* tail8 = 0;
				sqlite3_stmt *stmt;
				sqlite3_prepare(_tab.db, qstart8, -1, &stmt, &tail8);
				qstart8 = (char*)tail8;
				sqlite3_finalize(stmt);
				queryNo--;
			}

			TCHAR* err16 = utils::utf8to16(err8);
			TCHAR text16[_tcslen(err16) + 128];
			int lineNo = SendMessage(_tab.hEditorWnd, EM_LINEFROMCHAR, strlen(query8) - strlen(qstart8), 0);
			_stprintf(text16, TEXT("Error\n%ls\nat line %i"), err16, lineNo + 1);
			SetWindowText(hResultWnd, text16);
			delete [] err16;
			sqlite3_free(err8);
		}
		isMetaChanged = detectMetaChanges(query8);
		delete [] query8;

		if (prefs::get("synchronous-off"))
			sqlite3_exec(_tab.db, "pragma synchronous = 1", NULL, 0, NULL);

		if (isTrn)
			sqlite3_exec(_tab.db, err8 == 0 ? "commit" : "rollback", NULL, 0, NULL);

		ShowWindow(hResultWnd, SW_SHOW);
		SetWindowLong(_tab.hTabWnd, GWL_USERDATA, (LONG)hResultWnd); // Also see ACTION_UPDATETAB

		int tabNo = getTabNo(_tab.id);
		tab = &tabs[tabNo];
		if (tab->tabTooltips[0] != NULL)
			delete [] tab->tabTooltips[0];
		tab->tabTooltips[0] = utils::replaceAll(TEXT("Batch"), TEXT(" "), TEXT(" "));

		_stprintf(tab->queryElapsedTimes[0], TEXT("Elapsed: %.2fs"), (GetTickCount() - tBatchStart) / 1000.0);
		if (hTabWnd == tab->hTabWnd)
			SendMessage(hStatusWnd, SB_SETTEXT, SB_ELAPSED_TIME, (LPARAM)tab->queryElapsedTimes[0]);

		MessageBeep(0);
	} else {
		for (int resultNo = 0; resultNo < ListBox_GetCount(_tab.hQueryListWnd); resultNo++) {
			int size = ListBox_GetTextLen(_tab.hQueryListWnd, resultNo) + 1;
			TCHAR query16[size]{0};
			ListBox_GetText(_tab.hQueryListWnd, resultNo, query16);

			char* sql8 = utils::utf16to8(query16);
			DWORD tStart, tEnd;
			tStart = GetTickCount();

			sqlite3_stmt *stmt;
			int rc = sqlite3_prepare_v2(_tab.db, sql8, -1, &stmt, 0);
			if (rc == SQLITE_OK && _tab.isPlan && !sqlite3_stmt_isexplain(stmt)) {
				sqlite3_finalize(stmt);
				char* new8 = new char[strlen(sql8) + strlen("explain query plan ") + 1]{0};
				sprintf(new8, "explain query plan %s", sql8);
				delete [] sql8;
				sql8 = new8;
				rc = sqlite3_prepare_v2(_tab.db, sql8, -1, &stmt, 0);
			}
			int colCount = rc == SQLITE_OK ? sqlite3_column_count(stmt) : 0;

			if (sqlite3_bind_parameter_count(stmt))
				DialogBoxParam (GetModuleHandle(0), MAKEINTRESOURCE(IDD_BIND_PARAMETERS), hMainWnd, (DLGPROC)&dialogs::cbDlgBindParameters, (LPARAM)stmt);

			HWND hResultWnd = 0;
			HWND hRowsWnd = GetDlgItem(_tab.hTabWnd, IDC_TAB_ROWS + resultNo);
			HWND hMessageWnd = GetDlgItem(_tab.hTabWnd, IDC_TAB_MESSAGE + resultNo);
			int rowCount = 0;
			if (rc == SQLITE_OK && colCount > 0) {
				hResultWnd = hRowsWnd;
				rowCount = ListView_SetData(hResultWnd, stmt, true);
				rc = sqlite3_errcode(_tab.db);
				if (rc != SQLITE_OK && rc != SQLITE_DONE && rowCount == 0)
					hResultWnd = 0;
				else
					ListView_SetItemState (hResultWnd, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			}

			if (hResultWnd == 0) {
				hResultWnd = hMessageWnd;
				if (rc == SQLITE_OK && SQLITE_DONE == sqlite3_step(stmt)) {
					int cnt = sqlite3_changes(_tab.db);
					TCHAR text[64];
					_stprintf(text, TEXT("%i rows have been changed"), cnt);
					SetWindowText(hResultWnd, text);
					isMetaChanged = isMetaChanged || (cnt == 0 && detectMetaChanges(sql8));
				} else {
					char *err8 = (char*)sqlite3_errmsg(_tab.db);
					TCHAR* err16 = utils::utf8to16(err8);
					TCHAR text16[_tcslen(err16) + 128];
					int lineNo = ListBox_GetItemData(_tab.hQueryListWnd, resultNo);
					_stprintf(text16, TEXT("Error\n%ls\nat line %i"), err16, lineNo + 1);
					SetWindowText(hResultWnd, text16);
					delete [] err16;
				}
			}

			SetWindowLong(hResultWnd == hRowsWnd ? hMessageWnd : hRowsWnd, GWL_USERDATA, -1);

			if (rc == SQLITE_OK || rc == SQLITE_DONE)
				saveQuery("history", sql8);
			delete [] sql8;

			if (tab->tabTooltips[resultNo] != NULL)
				delete [] tab->tabTooltips[resultNo];
			TCHAR* exp_query16 = utils::utf8to16(sqlite3_expanded_sql(stmt));
			tab->tabTooltips[resultNo] = utils::replaceAll(exp_query16, TEXT("\t"), TEXT("    "));
			delete [] exp_query16;
			sqlite3_finalize(stmt);

			int tabNo = getTabNo(_tab.id);
			tab = &tabs[tabNo];

			tEnd = GetTickCount();
			_stprintf(tab->queryElapsedTimes[resultNo], TEXT("Elapsed: %.2fs"), (tEnd - tStart) / 1000.0);

			TCHAR caption[128];
			TCHAR resname[64]{0};
			if (_tcsncmp(tab->tabTooltips[resultNo], TEXT("--"), 2) == 0)
				_stscanf(tab->tabTooltips[resultNo], TEXT("-- %63[^\n]"), resname);
			if (_tcsncmp(tab->tabTooltips[resultNo], TEXT("explain query plan --"), 21) == 0)
				_stscanf(tab->tabTooltips[resultNo], TEXT("explain query plan -- %63[^\n]"), resname);
			if (_tcslen(resname) == 0)
				_stprintf(resname, TEXT("Result #%i"), resultNo + 1);

			if (rowCount < 0)
				_stprintf(caption, TEXT("%ls (Cut to %i rows)"), resname, -rowCount);
			if (rowCount == 0)
				_stprintf(caption, TEXT("%ls"), resname);
			if (rowCount > 0)
				_stprintf(caption, rowCount > 0 ? TEXT("%ls (%i rows)") : TEXT("%ls"), resname, rowCount);

			TCITEM tci;
			tci.mask = TCIF_TEXT | TCIF_IMAGE;
			tci.iImage = -1;
			tci.pszText = caption;
			tci.cchTextMax = _tcslen(caption);
			TabCtrl_InsertItem(tab->hTabWnd, resultNo, &tci);

			ShowWindow(hResultWnd, resultNo == 0 ? SW_SHOW : SW_HIDE);
			if (resultNo == 0) {
				SetWindowLong(tab->hTabWnd, GWL_USERDATA, (LONG)hResultWnd); // Also see ACTION_UPDATETAB
				if (hTabWnd == tab->hTabWnd)
					SendMessage(hStatusWnd, SB_SETTEXT, SB_ELAPSED_TIME, (LPARAM)tab->queryElapsedTimes[resultNo]);
			}
			cbEnumChildren(hResultWnd, ACTION_RESIZETAB);

			if (prefs::get("beep-query-duration") < (int)(tEnd - tStart))
				MessageBeep(0);
		}
	}

	int tabNo = getTabNo(_tab.id);
	tab = &tabs[tabNo];

	EnumChildWindows(tab->hTabWnd, (WNDENUMPROC)cbEnumChildren, ACTION_RESIZETAB);
	if (hTabWnd == tab->hTabWnd) {
		InvalidateRect(tab->hTabWnd, NULL, FALSE);
		updateTransactionState();
		updateExecuteMenu(true);
		Toolbar_SetButtonState(hToolbarWnd, IDM_INTERRUPT, TBSTATE_HIDDEN);
	}

	CloseHandle(tab->thread);
	tab->thread = 0;
	tab->isPlan = 0;
	tab->isBatch = 0;
	updateTransactionState();
	SendMessage(hMainTabWnd, WMU_TAB_SET_STYLE, getTabNo(_tab.id), 0);
	if (isMetaChanged)
		updateTree();

	return 1;
}

int executeEditorQuery(bool isPlan, bool isBatch, bool onlyCurrent, int vkKey) {
	int tabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
	TEditorTab* tab = &tabs[tabNo];
	tab->isBatch = isBatch;
	tab->isPlan = isPlan;

	if (tab->thread) {
		MessageBox(hMainWnd, TEXT("The tab is busy"), TEXT("Error"), MB_OK);
		return false;
	}

	CHARRANGE range;
	SendMessage(hEditorWnd, EM_EXGETSEL, 0, (LPARAM)&range);

	bool isSelection = range.cpMin != range.cpMax;
	int size =  isSelection ? range.cpMax - range.cpMin + 1 : GetWindowTextLength(hEditorWnd);
	if (size <= 0)
		return 0;

	if (vkKey)
		size = MAX(size, MAX_TEXT_LENGTH);

	TCHAR buf[size + 1]{0};
	if (vkKey == 0) {
		if (!SendMessage(hEditorWnd, isSelection ? EM_GETSELTEXT : WM_GETTEXT, size + 1, (LPARAM)buf))
			return 0;

		if (isSelection)
			onlyCurrent = false;
	} else {
		TCHAR* replacement16 = getCurrentText(hEditorWnd);
		sqlite3_stmt *stmt;
		if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select replace(query, '$SUB$', trim(?4)) from shortcuts where key = ?1 and ctrl = ?2 and alt = ?3 and length(query) > 0", -1, &stmt, 0)) {
			sqlite3_bind_int(stmt, 1, vkKey);
			sqlite3_bind_int(stmt, 2, HIWORD(GetKeyState(VK_CONTROL)) > 0);
			sqlite3_bind_int(stmt, 3, HIWORD(GetKeyState(VK_MENU)) > 0);
			char* replacement8 = utils::utf16to8(replacement16);
			sqlite3_bind_text(stmt, 4, replacement8, strlen(replacement8), SQLITE_TRANSIENT);
			delete [] replacement8;

			if(SQLITE_ROW == sqlite3_step(stmt)) {
				TCHAR* query16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
				_tcscpy(buf, query16);
				delete [] query16;
			}
		} else {
			showDbError(hMainWnd);
		}
		sqlite3_finalize(stmt);
		delete [] replacement16;

		if (_tcslen(buf) == 0)
			return 0;
	}

	// Sometimes EM_GETSELTEXT returns a text without new line as \r without \n
	for (int i = 0; isSelection && _tcschr(buf, TEXT('\r')) && (i < size + 1); i++)
		buf[i] = buf[i] == TEXT('\r') && buf[i + 1] != TEXT('\n') ? TEXT('\n') : buf[i];

	TCHAR bufcopy[size + 1];
	_tcscpy(bufcopy, buf);

	ListBox_ResetContent(tab->hQueryListWnd);

	int start = 0;
	char quote = 0;
	int inComment = 0; // 0 - plain text, 1 - inside "--"-comment, 2 - inside "/* */"-comment

	for (int i = 0; i < size; i++) {
		if (buf[i] == TEXT('"') || buf[i] == TEXT('\''))
			quote = (quote == 0) && (buf[i] != quote) ? buf[i] : 0;

		if (inComment == 1 && buf[i] == TEXT('\n'))
			inComment = 0;

		if (inComment == 2 && buf[i] == TEXT('*') && buf[i + 1] == TEXT('/'))
			inComment = 0;

		if ((quote != 0 || inComment > 0) && (i != size - 1))
			continue;

		inComment = buf[i] == TEXT('-') && buf[i + 1] == TEXT('-') ? 1 : buf[i] == TEXT('/') && buf[i + 1] == TEXT('*') ? 2 : 0;
		if (inComment > 0 && (i != size - 1))
			continue;

		if (buf[i] == ';' || (i == size - 1)) {
			TCHAR query16[i - start + 2]{0};
			_tcsncpy(query16, buf + start, i - start + 1);

			char* query8 = utils::utf16to8(query16);
			if (sqlite3_complete(query8) || (i == size - 1)) {
				if (query16[_tcslen(query16) - 1] == TEXT(';')) // remove last ;
					query16[_tcslen(query16) - 1] = 0;
				TCHAR* trimmed16 = utils::trim(query16);
				if(_tcslen(trimmed16) > 1 && (!onlyCurrent || (onlyCurrent && i >= MIN(range.cpMin, range.cpMax)))) {
					int idx = ListBox_AddString(tab->hQueryListWnd, trimmed16);

					TCHAR ignores[] = TEXT(" \r\n\t;");
					int lineStart = start;
					while (buf[lineStart] && _tcschr(ignores, buf[lineStart]) != 0)
						lineStart++;

					int lineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, lineStart, 0) + (isSelection ? SendMessage(hEditorWnd, EM_LINEFROMCHAR, range.cpMin, 0) : 0);
					ListBox_SetItemData(tab->hQueryListWnd, idx, lineNo);

					if (onlyCurrent)
						break;
				}
				start = i + 1;
			}
		}
	}

	int queryCount = ListBox_GetCount(tab->hQueryListWnd);
	if (!tab->isBatch && queryCount >= MAX_RESULT_COUNT) {
		if (IDYES != MessageBox(hMainWnd, TEXT("Max number of results (32) will be exceeded.\nDo you want to execute queries as batch?"), NULL, MB_YESNO | MB_ICONQUESTION))
			return false;
		tab->isBatch = true;
	}

	if (tab->isBatch) {
		ListBox_ResetContent(tab->hQueryListWnd);
		ListBox_AddString(tab->hQueryListWnd, bufcopy);
	}

	TabCtrl_DeleteAllItems(tab->hTabWnd);
	EnumChildWindows(tab->hTabWnd, (WNDENUMPROC)cbEnumChildren, ACTION_DESTROY);

	if (tab->isBatch) {
		HWND hMessageWnd = CreateWindow(WC_STATIC, TEXT("Done"), WS_CHILD | SS_LEFT, 20, 20, 100, 100, tab->hTabWnd, (HMENU)IDC_TAB_MESSAGE, GetModuleHandle(0), NULL);
		SetWindowLong(hMessageWnd, GWL_USERDATA, 0);
	} else {
		for (int resultNo = 0; resultNo < queryCount; resultNo++) {
			HWND hRowsWnd = CreateWindow(WC_LISTVIEW, NULL, WS_TABSTOP | WS_CHILD | LVS_AUTOARRANGE | LVS_REPORT | LVS_SHOWSELALWAYS, 20, 20, 100, 100, tab->hTabWnd, (HMENU)(IDC_TAB_ROWS + resultNo), GetModuleHandle(0), NULL);
			HWND hMessageWnd = CreateWindow(WC_STATIC, NULL, WS_CHILD | SS_LEFT, 20, 20, 100, 100, tab->hTabWnd, (HMENU)(IDC_TAB_MESSAGE + resultNo), GetModuleHandle(0), NULL);
			CreateWindow(WC_LISTBOX, NULL, WS_CHILD, 300, 0, 400, 100, hRowsWnd, (HMENU)IDC_REFLIST, GetModuleHandle(0), 0);
			SetWindowLong(hRowsWnd, GWL_USERDATA, resultNo);
			SetWindowLong(hMessageWnd, GWL_USERDATA, resultNo);
		}
	}

	tab->thread = (HANDLE)_beginthreadex(0, 0, &processEditorQuery, tab, 0, 0);
	return 1;
}

void updateRecentList() {
	HMENU hMenu = GetSubMenu(hMainMenu, 0);
	int size = GetMenuItemCount(hMenu);
	int afterRecentCount = 5 + isCipherSupport; // exit, sep, setting, attach, sep + cipher, save as

	for (int i = 4; i < size - afterRecentCount; i++)
		RemoveMenu(hMenu, 4, MF_BYPOSITION);

	int count = 0;
	sqlite3_stmt* stmt;
	if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select path from recents order by time desc limit 100", -1, &stmt, 0)) {
		while(SQLITE_ROW == sqlite3_step(stmt) && count < prefs::get("recent-count")) {
			TCHAR* path16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
			if (utils::isFileExists(path16)) {
				MENUITEMINFO mi = {0};
				mi.cbSize = sizeof(MENUITEMINFO);
				mi.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
				mi.wID = IDM_RECENT + count;
				mi.dwTypeData = path16;
				InsertMenuItem(hDbMenu, 4 + count, true, &mi);
				count++;
			}
			delete [] path16;
		}
	}
	sqlite3_finalize(stmt);

	if (count == 0) {
		MENUITEMINFO mi = {0};
		mi.cbSize = sizeof(MENUITEMINFO);
		mi.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_ID | MIIM_STATE;
		mi.wID = IDM_RECENT + count;
		mi.fState = MF_GRAYED | MF_DISABLED;
		mi.dwTypeData = TEXT("No recents");
		InsertMenuItem(hDbMenu, 4 + count, true, &mi);
	}
};

void suggestCLIQuery(int key) {
	sqlite3_stmt *stmt;
	char* dbname8 = utils::getFileName(sqlite3_db_filename(db, 0));

	int size = GetWindowTextLength(cli.hEditorWnd);
	TCHAR sql16[size + 1] = {0};
	GetWindowText(cli.hEditorWnd, sql16, size + 1);
	char *sql8 = utils::utf16to8(sql16);

	int rc = SQLITE_OK == sqlite3_prepare_v2(prefs::db,
		key == VK_UP ? "select time, query from cli where dbname = ?1 and time < coalesce(?2, time + 1) and query <> ?3 order by time desc limit 1" :
		key == VK_DOWN ? "select time, query from cli where dbname = ?1 and time > coalesce(?2, time - 1) and query <> ?3 order by time asc limit 1" :
		key == VK_TAB ? "select time, query from cli where dbname = ?1 and query like '%' || ?2 || '%'  and query <> ?3 limit 1" : "", -1, &stmt, 0);
	sqlite3_bind_text(stmt, 1, dbname8, strlen(dbname8), SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, sql8, strlen(sql8), SQLITE_TRANSIENT);

	if (rc && (key == VK_UP || key == VK_DOWN)) {
		int time = GetWindowLong(cli.hEditorWnd, GWL_USERDATA);
		if (time)
			sqlite3_bind_int(stmt, 2, time);
		else
			sqlite3_bind_null(stmt, 2);

		if (SQLITE_ROW == sqlite3_step(stmt)) {
			SetWindowLong(cli.hEditorWnd, GWL_USERDATA, sqlite3_column_int(stmt, 0));
			TCHAR* sql16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 1));
			SetWindowText(cli.hEditorWnd, sql16);
			delete [] sql16;
		}
	}

	if (rc && (key == VK_TAB)) {
		int size = GetWindowTextLength(cli.hEditorWnd);
		TCHAR text16[size + 1] = {0};
		GetWindowText(cli.hEditorWnd, text16, size + 1);
		char *text8 = utils::utf16to8(text16);
		sqlite3_bind_text(stmt, 2, text8, strlen(text8), SQLITE_TRANSIENT);
		delete [] text8;

		if (SQLITE_ROW == sqlite3_step(stmt)) {
			TCHAR* sql16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 1));
			SetWindowText(cli.hEditorWnd, sql16);

			delete [] sql16;
		}
	}

	sqlite3_finalize(stmt);
	delete [] dbname8;
	delete [] sql8;
}

void loadCLIResults(int cnt) {
	sqlite3_stmt *stmt;
	if (SQLITE_OK == sqlite3_prepare_v2(prefs::db,
		"with t as ("\
		"select query || char(10) || result a from cli order by time desc limit ?2) " \
		"select group_concat(a, '') from t", -1, &stmt, 0)) {
		char* dbname8 = utils::getFileName(sqlite3_db_filename(db, 0));
		sqlite3_bind_text(stmt, 1, dbname8, strlen(dbname8), SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 2, cnt);
		delete [] dbname8;
		if (SQLITE_ROW == sqlite3_step(stmt)) {
			TCHAR* text16 = utils::utf8to16((const char*)sqlite3_column_text(stmt, 0));
			SetWindowText(cli.hResultWnd, text16);
			delete [] text16;
		} else {
			showDbError(hMainWnd);
		}
	}
	sqlite3_finalize(stmt);

	setEditorColor(cli.hResultWnd, RGB(0, 196, 0), true);
	RedrawWindow(cli.hResultWnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME);
}

bool updateReferences() {
	char query8[1024] = {0};
	sqlite3_stmt *stmt, *stmt2;
	bool rc = SQLITE_OK == sqlite3_prepare_v2(db,
		"select m.tbl_name, fk.\"from\", fk.\"table\", fk.\"to\" " \
		"from sqlite_master m, pragma_foreign_key_list(m.tbl_name) fk " \
		"where m.type = 'table' ",
		-1, &stmt, 0);
	bool rc2 = SQLITE_OK == sqlite3_prepare_v2(prefs::db,
		"insert or ignore into refs (dbname, schema, tblname, colname, query) " \
		"select ?1, ?2, ?3, ?4, ?5", -1, &stmt2, 0);

	char* dbname8 = utils::getFileName(sqlite3_db_filename(db, 0));
	char schema8[] = "main";
	while (rc && rc2 && (SQLITE_ROW == sqlite3_step(stmt))) {
		char* tblname8 = (char*)sqlite3_column_text(stmt, 0);
		char* colname8 = (char*)sqlite3_column_text(stmt, 1);
		sprintf(query8, "select upper('%s'), * from \"main\".\"%s\" t where t.\"%s\" = ?1",
			(char*)sqlite3_column_text(stmt, 2), (char*)sqlite3_column_text(stmt, 2), (char*)sqlite3_column_text(stmt, 3));
		sqlite3_bind_text(stmt2, 1, dbname8, strlen(dbname8), SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt2, 2, schema8, strlen(schema8), SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt2, 3, tblname8, strlen(tblname8), SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt2, 4, colname8, strlen(colname8), SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt2, 5, query8, strlen(query8), SQLITE_TRANSIENT);
		sqlite3_step(stmt2);
		sqlite3_reset(stmt2);
	}
	sqlite3_finalize(stmt);
	sqlite3_finalize(stmt2);

	delete [] dbname8;
	return true;
}

bool attachDb(sqlite3** _db, const char* path8, const char* _name8) {
	char* name8 = utils::getFileName(path8, true);
	char query8[2 * strlen(path8) + 1024]{0};
	sprintf(query8, "attach database '%s' as '%s'", path8, _name8 ? _name8 : name8);

	auto runEverywhere = [](const char* query8) {
		for (int tabNo = 0; tabNo < MAX_TAB_COUNT; tabNo++)	{
			if (tabs[tabNo].id && tabs[tabNo].db)
				sqlite3_exec(tabs[tabNo].db, query8, 0, 0 , 0);
		}

		if (cli.db)
			sqlite3_exec(cli.db, query8, 0, 0 , 0);
	};

	if (isCipherSupport) {
		sprintf(query8, "attach database '%s' as '%s' key ''", path8, _name8 ? _name8 : name8);
		sqlite3_stmt* stmt;
		sqlite3_prepare_v2(prefs::db,
			"select param, value " \
			"from (select param, value, no from main.encryption where dbpath = ?1 union select param, value, no from temp.encryption where dbpath = ?1) " \
			"order by no", -1, &stmt, 0);
		sqlite3_bind_text(stmt, 1, path8, strlen(path8), SQLITE_TRANSIENT);
		while (SQLITE_ROW == sqlite3_step(stmt)) {
			const char* param8 = (const char*)sqlite3_column_text(stmt, 0);
			const char* value8 = (const char*)sqlite3_column_text(stmt, 1);
			if (strcmp(param8, "key") == 0) {
				sprintf(query8, "attach database '%s' as '%s' key '%s'", path8, _name8 ? _name8 : name8, value8);
			} else {
				char pragma8[1024];
				sprintf(pragma8, utils::isNumber(value8, 0) ? "pragma %s = %s" : "pragma %s = '%s'", param8, value8);
				sqlite3_exec(*_db, pragma8, 0, 0, 0);
				if (*_db == db)
					runEverywhere(pragma8);
			}
		}
		sqlite3_finalize(stmt);
	}
	delete [] name8;

	bool rc = SQLITE_OK == sqlite3_exec(*_db, query8, 0, 0 , 0);
	if (*_db != db)
		return rc;

	if(!rc && (!isCipherSupport || (isCipherSupport && DLG_OK != DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_ENCRYPTION), hMainWnd, (DLGPROC)&dialogs::cbDlgEncryption, (LPARAM)path8)))) {
		showDbError(hMainWnd);
		return false;
	}

	runEverywhere(query8);

	return true;
}

bool openConnection(sqlite3** _db, const char* path8, bool isReadOnly) {
	int mode = (db == *_db && isReadOnly) || (db != *_db && sqlite3_db_readonly(db, 0)) ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
	SendMessage(hMainWnd, WMU_SET_ICON, 0, 0);
	sqlite3_open_v2(path8 && strlen(path8) > 0 ? path8 : "file::memory:?cache=shared", _db, mode | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI, NULL);
	if (SQLITE_OK != sqlite3_exec(*_db, "select 1 from sqlite_master limit 1", 0, 0, 0)) {
		bool rc = false;
		if (isCipherSupport) {
			sqlite3_stmt *stmt;
			if (SQLITE_OK == sqlite3_prepare_v2(prefs::db,
				"select param, value " \
				"from (select param, value, no from main.encryption where dbpath = ?1 union select param, value, no from temp.encryption where dbpath = ?1) " \
				"order by no", -1, &stmt, 0)) {
				sqlite3_bind_text(stmt, 1, path8, strlen(path8), SQLITE_TRANSIENT);
				while (SQLITE_ROW == sqlite3_step(stmt)) {
					char query8[1024];
					const char* param8 = (const char*)sqlite3_column_text(stmt, 0);
					const char* value8 = (const char*)sqlite3_column_text(stmt, 1);
					sprintf(query8, utils::isNumber(value8, 0) ? "pragma %s = %s" : "pragma %s = '%s'", param8, value8);
					sqlite3_exec(*_db, query8, 0, 0, 0);
				}
				rc = SQLITE_OK == sqlite3_exec(db, "select 1 from sqlite_master limit 1", 0, 0, 0);

			}
			sqlite3_finalize(stmt);

			if (!rc)
				rc = DLG_OK == DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_ENCRYPTION), hMainWnd, (DLGPROC)&dialogs::cbDlgEncryption, (LPARAM) *_db);

			if (rc)
				SendMessage(hMainWnd, WMU_SET_ICON, 0, 1);
		}

		if (!rc) {
			MessageBox(hMainWnd, TEXT("Unable to open a new connection to the database"), TEXT("Error"), MB_OK | MB_ICONSTOP);
			sqlite3_close_v2(*_db);
			*_db = 0;
			return false;
		}
	}

	// load extensions
	sqlite3_enable_load_extension(*_db, true);
	if (prefs::get("autoload-extensions")) {
		TCHAR extensions[2048] = TEXT(" Loaded extensions: ");
		TCHAR searchPath[MAX_PATH]{0};
		_stprintf(searchPath, TEXT("%ls\\extensions\\*.dll"), APP_PATH);

		WIN32_FIND_DATA ffd;
		HANDLE hFind = FindFirstFile(searchPath, &ffd);
		bool isLoad = false;

		if (hFind != INVALID_HANDLE_VALUE) {
			do {
				TCHAR file16[MAX_PATH]{0};
				_stprintf(file16, TEXT("%ls/extensions/%ls"), APP_PATH, ffd.cFileName);
				char* file8 = utils::utf16to8(file16);

				if (SQLITE_OK == sqlite3_load_extension(*_db, file8, NULL, NULL)) {
					if (isLoad)
						_tcscat(extensions, TEXT(", "));

					TCHAR filename16[256]{0};
					_tsplitpath(ffd.cFileName, NULL, NULL, filename16, NULL);
					_tcscat(extensions, filename16);
					isLoad = true;
				}
				delete [] file8;
			} while ((FindNextFile(hFind, &ffd)));
			FindClose(hFind);
		}

		if(*_db == db)
			SendMessage(hStatusWnd, SB_SETTEXT, SB_EXTENSIONS, (LPARAM)(isLoad ? extensions : TEXT("")));
	}

	// attach databases
	if (*_db != db) {
		sqlite3_stmt* stmt;
		if (SQLITE_OK == sqlite3_prepare_v2(db, "select file from pragma_database_list t where seq > 0 and name <> 'shared'", -1, &stmt, 0)) {
			while (SQLITE_ROW == sqlite3_step(stmt))
				attachDb(_db, (const char*)sqlite3_column_text(stmt, 0));
		}
		sqlite3_finalize(stmt);
	}

	sqlite3_exec(*_db, "attach database 'file::memory:?cache=shared' as shared", NULL, NULL, NULL);

	if (prefs::get("use-legacy-rename"))
		sqlite3_exec(*_db, "pragma legacy_alter_table = 1", 0, 0, 0);

	char* startup8 = prefs::get("startup", "");
	if (strlen(startup8) > 0)
		sqlite3_exec(*_db, startup8, 0, 0, 0);
	delete [] startup8;

	return true;
}

bool openDb(const TCHAR* path) {
	if (!closeDb())
		return false;

	ULONG attrs = GetFileAttributes(path);
	bool isReadOnly = ((attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_READONLY)) || HIWORD(GetKeyState(VK_CONTROL));

	SetWindowText(hMainWnd, TEXT("Loading database..."));
	char* path8 = utils::utf16to8(path);
	if (!openConnection(&db, path8, isReadOnly)) {
		delete [] path8;
		SetWindowText(hMainWnd, TEXT("No database selected"));
		return false;
	}

	updateReferences();

	if (!IsWindow(hMainWnd)) {
		delete [] path8;
		return true;
	}
	SetWindowText(hMainWnd, path);

	TCHAR prev[MAX_PATH];
	GetMenuString(hDbMenu, 4, prev, MAX_PATH, MF_BYPOSITION); // Open, Save as, Close, Separator

	if (_tcscmp(prev, path) != 0) {
		sqlite3_stmt* stmt;
		if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "replace into recents (path, time) values (?1, ?2)", -1, &stmt, 0)) {
			sqlite3_bind_text(stmt, 1, path8, strlen(path8),  SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, std::time(0));
			sqlite3_step(stmt);
		}
		sqlite3_finalize(stmt);

		updateRecentList();
	}
	delete [] path8;

	TCHAR title[_tcslen(path) + 30]{0};
	_stprintf(title, TEXT("%ls%ls"), path, isReadOnly ? TEXT(" (read only)") : TEXT(""));
	SetWindowText(hMainWnd, title);

	updateTree();
	EnableWindow(hTreeWnd, true);
	updateTransactionState();
	enableMainMenu();

	if (!PRAGMAS[0]) {
		sqlite3_stmt *stmt;
		int rc = sqlite3_prepare_v2(db, "select name from pragma_pragma_list()", -1, &stmt, 0);
		int rowNo = 0;
		while (rc == SQLITE_OK && SQLITE_ROW == sqlite3_step(stmt) && rowNo < MAX_ENTITY_COUNT) {
			PRAGMAS[rowNo] = utils::utf8to16((char *) sqlite3_column_text(stmt, 0));
			rowNo++;
		}
		PRAGMAS[rowNo] = 0;
		sqlite3_finalize(stmt);
	}

	if (!FUNCTIONS[0]) {
		sqlite3_stmt *stmt;
		int rc = sqlite3_prepare_v2(db, "select distinct name from pragma_function_list() union select name from pragma_module_list where name not like 'pragma_%'", -1, &stmt, 0);
		int rowNo = 0;
		while (rc == SQLITE_OK && SQLITE_ROW == sqlite3_step(stmt)) {
			FUNCTIONS[rowNo] = utils::utf8to16((char *) sqlite3_column_text(stmt, 0));
			rowNo++;
		}
		FUNCTIONS[rowNo] = 0;
		sqlite3_finalize(stmt);
	}

	// restore cli
	if(prefs::get("restore-editor"))
		loadCLIResults(30);

	processHighlight(hEditorWnd, true, true, true);

	startHttpServer();

	return true;
}

bool closeConnections () {
	for (int i = 0; i < MAX_TAB_COUNT; i++) {
		TEditorTab* tab = &tabs[i];
		if (tab->id && tab->db) {
			sqlite3_exec(tab->db, "detach database shared", 0, 0, 0);
			if (SQLITE_OK != sqlite3_close(tab->db))
				showDbError(hMainWnd);
			tab->db = 0;
		}
	}

	if (cli.db) {
		sqlite3_exec(cli.db, "detach database shared", 0, 0, 0);
		if (SQLITE_OK != sqlite3_close(cli.db))
			showDbError(hMainWnd);
		cli.db = 0;
	}

	return true;
}

bool closeDb() {
	if (!db)
		return true;

	if (isDbBusy())
		return false;

	int nDlgOpen = 0;
	for (int i = 0; i < MAX_DIALOG_COUNT; i++)
		nDlgOpen += hDialogs[i] && IsWindow(hDialogs[i]);

	if (nDlgOpen > 0) {
		MessageBox(hMainWnd, nDlgOpen == 1 ?
			TEXT("There is open dialog. You should close it.") :
			TEXT("There are open dialogs. You should close them."),
			TEXT("Info"), MB_OK | MB_ICONWARNING);
		return false;
	}

	closeConnections();
	stopHttpServer();

	if (db && prefs::get("retain-passphrase") == 0) {
		sqlite3_stmt* stmt;
		if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "delete from temp.encryption where dbpath = ?1", -1, &stmt, 0)) {
			const char* dbpath = sqlite3_db_filename(db, 0);
			sqlite3_bind_text(stmt, 1, dbpath, strlen(dbpath), SQLITE_TRANSIENT);
			sqlite3_step(stmt);
		}
		sqlite3_finalize(stmt);
	}

	sqlite3_exec(db, "detach database shared", 0, 0, 0);
	if(SQLITE_OK != sqlite3_close(db))
		showDbError(hMainWnd);
	db = 0;

	SetWindowText(hMainWnd, TEXT("No database selected"));
	EnableWindow(hTreeWnd, false);
	TreeView_DeleteAllItems(hTreeWnd);
	SendMessage(hMainWnd, WMU_SET_ICON, 0, 0);

	for (int i = 0; (i < MAX_ENTITY_COUNT) && (TABLES[i] != 0); i++) {
		delete [] TABLES[i];
		TABLES[i] = 0;
	}

	disableMainMenu();
	updateExecuteMenu(false);

	return true;
}

bool isDbBusy() {
	bool isRunning = false;
	for (int i = 0; i < MAX_TAB_COUNT; i++)
		isRunning = tabs[i].thread != 0 || isRunning;

	isRunning = cli.thread != 0 || isRunning;

	if (isRunning)
		MessageBox(hMainWnd, TEXT("There are executing queries. You should stop them."), TEXT("Info"), MB_OK | MB_ICONWARNING);

	return isRunning;
}

int enableDbObject(const char* name8, int type) {
	bool rc = true;
	sqlite3_stmt *stmt;
	sqlite3_prepare_v2(prefs::db, "select sql, rowid from disabled where name = ?1 and type = ?2 and dbpath = ?3", -1, &stmt, 0);
	sqlite3_bind_text(stmt, 1, name8, strlen(name8), SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
	const char* dbpath = sqlite3_db_filename(db, 0);
	sqlite3_bind_text(stmt, 3, dbpath, strlen(dbpath), SQLITE_TRANSIENT);

	if (SQLITE_ROW == sqlite3_step(stmt) && SQLITE_OK == sqlite3_exec(db, (char*)sqlite3_column_text(stmt, 0), 0, 0, 0)) {
		char query8[128];
		sprintf(query8, "delete from disabled where rowid = %i", sqlite3_column_int(stmt, 1));
		sqlite3_exec(prefs::db, query8, 0, 0, 0);
	} else {
		showDbError(hMainWnd);
		rc = false;
	}
	sqlite3_finalize(stmt);

	return rc;
}

int disableDbObject(const char* name8, int type) {
	bool rc = true;

	sqlite3_stmt *stmt;
	sqlite3_prepare_v2(db, "select sql from sqlite_master where type = ?2 and name = coalesce(?1, name)", -1, &stmt, 0);
	sqlite3_bind_text(stmt, 1, name8, strlen(name8), SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
	rc = SQLITE_ROW == sqlite3_step(stmt);
	char* sql = 0;
	if (rc) {
		sql = new char[sqlite3_column_bytes(stmt, 0) + 1]{0};
		strcpy(sql, (const char*)sqlite3_column_text(stmt, 0));
	}
	sqlite3_finalize(stmt);

	if (!rc)
		return false;

	sqlite3_prepare_v2(prefs::db, "replace into disabled (name, type, dbpath, sql) select ?1, ?2, ?3, ?4", -1, &stmt, 0);
	sqlite3_bind_text(stmt, 1, name8, strlen(name8), SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
	const char* dbpath = sqlite3_db_filename(db, 0);
	sqlite3_bind_text(stmt, 3, dbpath, strlen(dbpath), SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, sql, strlen(sql), SQLITE_TRANSIENT);

	char query8[128];
	sprintf(query8, "drop %s \"%s\"", TYPES8[type], name8);
	if (SQLITE_DONE != sqlite3_step(stmt) || SQLITE_OK != sqlite3_exec(db, query8, 0, 0, 0)) {
		showDbError(hMainWnd);
		rc = false;
	}
	sqlite3_finalize(stmt);
	delete [] sql;
	return rc;
}

void setEditorFont(HWND hWnd) {
	char* family8 = prefs::get("font-family", "Courier New"); // Only TrueType
	TCHAR* family16 = utils::utf8to16(family8);

	CHARFORMAT cf = {0};
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_FACE | CFM_SIZE;
	cf.bCharSet = DEFAULT_CHARSET;
	cf.bPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
	cf.yHeight = (hWnd != cli.hResultWnd ? prefs::get("font-size") : prefs::get("cli-font-size")) * 20;
	_tcscpy(cf.szFaceName, family16);
	SendMessage(hWnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

	delete [] family8;
	delete [] family16;
}

void setEditorColor(HWND hWnd, COLORREF color, bool noEffects) {
	CHARFORMAT cf = {0};
	cf.cbSize = sizeof(cf);
	cf.dwMask = noEffects ? CFM_COLOR | CFM_EFFECTS : CFM_COLOR;
	cf.dwEffects = 0;
	cf.crTextColor = color;
	SendMessage(hWnd, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);
}

void setTreeFont(HWND hWnd) {
	HDC hDC = GetDC(hTreeWnd);
	char* fontFamily8 = prefs::get("font-family", "Courier New"); // Only TrueType
	TCHAR* fontFamily16 = utils::utf8to16(fontFamily8);
	HFONT hFont = CreateFont (-MulDiv(prefs::get("font-size"), GetDeviceCaps(hDC, LOGPIXELSY), 72), 0, 0, 0,
		FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS,
		fontFamily16);

	SendMessage (hWnd, WM_SETFONT, WPARAM (hFont), TRUE);
	ReleaseDC(hWnd, hDC);

	delete [] fontFamily8;
	delete [] fontFamily16;
}

void updateExecuteMenu(bool isEnable) {
	int state = isEnable ? MF_BYCOMMAND | MF_ENABLED : MF_BYCOMMAND | MF_GRAYED;
	HMENU hMenu = GetSubMenu(hMainMenu, 1);
	EnableMenuItem(hMenu, IDM_PLAN, state);
	EnableMenuItem(hMenu, IDM_EXECUTE, state);
	EnableMenuItem(hMenu, IDM_EXECUTE_BATCH, state);

	state = isEnable ? TBSTATE_ENABLED : TBSTATE_INDETERMINATE;
	Toolbar_SetButtonState(hToolbarWnd, IDM_PLAN, state);
	Toolbar_SetButtonState(hToolbarWnd, IDM_EXECUTE, state);
}

void disableMainMenu() {
	HMENU hMenu;
	hMenu = GetSubMenu(hMainMenu, 0);
	EnableMenuItem(hMenu, IDM_CLOSE, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(hMenu, IDM_SAVE_AS, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(hMenu, IDM_ATTACH, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(hMenu, IDM_ENCRYPTION, MF_BYCOMMAND | MF_GRAYED);

	hMenu = GetSubMenu(hMainMenu, 1);
	EnableMenuItem(hMenu, IDM_PLAN, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(hMenu, IDM_EXECUTE, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(hMenu, IDM_EXECUTE_BATCH, MF_BYCOMMAND | MF_GRAYED);

	hMenu = GetSubMenu(hMainMenu, 2);
	for (int i = 0; i < GetMenuItemCount(hMenu); i++)
		EnableMenuItem(hMenu, i, MF_BYPOSITION | MF_GRAYED);

	Toolbar_SetButtonState(hToolbarWnd, IDM_CLOSE, TBSTATE_INDETERMINATE);
	Toolbar_SetButtonState(hToolbarWnd, IDM_PLAN, TBSTATE_INDETERMINATE);
	Toolbar_SetButtonState(hToolbarWnd, IDM_EXECUTE, TBSTATE_INDETERMINATE);
}

void enableMainMenu() {
	HMENU hMenu;

	// Database
	hMenu = GetSubMenu(hMainMenu, 0);
	EnableMenuItem(hMenu, IDM_CLOSE, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(hMenu, IDM_SAVE_AS, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(hMenu, IDM_ATTACH, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(hMenu, IDM_ENCRYPTION, MF_BYCOMMAND | MF_ENABLED);

	// Query
	hMenu = GetSubMenu(hMainMenu, 1);
	EnableMenuItem(hMenu, IDM_PLAN, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(hMenu, IDM_EXECUTE, MF_BYCOMMAND | MF_ENABLED);
	EnableMenuItem(hMenu, IDM_EXECUTE_BATCH, MF_BYCOMMAND | MF_ENABLED);

	// Tools
	hMenu = GetSubMenu(hMainMenu, 2);
	for (int i = 0; i < GetMenuItemCount(hMenu); i++)
		EnableMenuItem(hMenu, i, MF_BYPOSITION | MF_ENABLED);

	Toolbar_SetButtonState(hToolbarWnd, IDM_CLOSE, TBSTATE_ENABLED);
	Toolbar_SetButtonState(hToolbarWnd, IDM_PLAN, TBSTATE_ENABLED);
	Toolbar_SetButtonState(hToolbarWnd, IDM_EXECUTE, TBSTATE_ENABLED);
	Toolbar_SetButtonState(hToolbarWnd, IDM_INTERRUPT, TBSTATE_HIDDEN);
}

int Toolbar_SetButtonState(HWND hToolbar, int id, byte state, LPARAM lParam) {
	TBBUTTONINFO tbi{0};
	tbi.cbSize = sizeof(TBBUTTONINFO);
	tbi.dwMask = TBIF_STATE | TBIF_LPARAM;
	tbi.fsState = state;
	tbi.lParam = lParam;

	return SendMessage(hToolbar, TB_SETBUTTONINFO, id, (LPARAM)&tbi);
}

bool CALLBACK cbEnumChildren (HWND hWnd, LPARAM action) {
	if (hWnd == NULL)
		return true;

	if (LOWORD(action) == ACTION_DESTROY) {
		int exceptId = HIWORD(action);
		if (exceptId && GetWindowLong(hWnd, GWL_ID) == exceptId)
			return true;
		DestroyWindow(hWnd);
	}

	if (action == ACTION_SETDEFFONT) {
		SendMessage(hWnd, WM_SETFONT, (LPARAM)hDefFont, true);
	}

	if (action == ACTION_RESIZETAB && GetParent(hWnd) == hTabWnd) {
		RECT rect, rect2;
		GetClientRect(hTabWnd, &rect);
		TabCtrl_GetItemRect(hTabWnd, 0, &rect2);

		SetWindowPos(hWnd, 0, rect.left + 5, rect.top + rect2.bottom + 5, rect.right - rect.left - 10, rect.bottom - rect.top - rect2.bottom - 10, 0);
	}

	if (action == ACTION_UPDATETAB && GetParent(hWnd) == hTabWnd) {
		int resultNo = TabCtrl_GetCurSel(hTabWnd);
		if (GetWindowLong(hWnd, GWL_USERDATA) == resultNo) {
			SetWindowLong(hTabWnd, GWL_USERDATA, (LONG)hWnd);
			ShowWindow(hWnd, SW_SHOW);
			SetFocus(hWnd);
			int pos = ListView_GetNextItem(hWnd, -1, LVNI_SELECTED);
			ListView_SetItemState (hWnd, pos == -1 ? 0 : pos, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			int tabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
			SendMessage(hStatusWnd, SB_SETTEXT, SB_ELAPSED_TIME, (LPARAM)tabs[tabNo].queryElapsedTimes[resultNo]);
		} else {
			ShowWindow(hWnd, SW_HIDE);
		}
	}

	return true;
}

void updateSizes(bool isPadding) {
	RECT rc, rcStatus, rcToolbar;
	GetClientRect(hMainWnd, &rc);
	GetClientRect(hStatusWnd, &rcStatus);
	GetClientRect(hToolbarWnd, &rcToolbar);

	top = rcToolbar.bottom + 3;
	int h = rc.bottom - rcStatus.bottom - rcToolbar.bottom - 3;

	int splitterW = prefs::get("splitter-width");
	int splitterH = prefs::get("splitter-height");
	int tabH = GetSystemMetrics(SM_CYMENU);

	SetWindowPos(hTreeWnd, 0, 0, top, splitterW, h, SWP_NOZORDER );
	SetWindowPos(hMainTabWnd, 0, splitterW + 5, top + 2, rc.right - splitterW - 5, tabH, SWP_NOZORDER);
	SetWindowPos(hEditorWnd, 0, splitterW + 5, top + tabH + 2, rc.right - splitterW - 6, splitterH - tabH - 2, SWP_NOZORDER);
	SetWindowPos(hTabWnd, 0, splitterW + 5, top + splitterH + 5, rc.right - splitterW - 5, h - splitterH - 4, SWP_NOZORDER);
	SetWindowPos(cli.hResultWnd, 0, splitterW + 5, top + splitterH + 5, rc.right - splitterW - 5, h - splitterH - 4, SWP_NOZORDER);

	if (isPadding) {
		RECT rc;
		GetClientRect(hEditorWnd, &rc);
		rc.left += 5;
		rc.top += 5;
		rc.right -= 5;
		rc.bottom -= 5;
		SendMessage(hEditorWnd, EM_SETRECT, 0, (LPARAM)&rc);
	}

	if (hTabWnd) {
		if (isPadding) {
			EnumChildWindows(hTabWnd, (WNDENUMPROC)cbEnumChildren, ACTION_RESIZETAB);
		} else {
			HWND hResultWnd = (HWND)GetWindowLong(hTabWnd, GWL_USERDATA);
			SetWindowPos(hResultWnd, 0, 0, 0, rc.right - splitterW - 15, h - splitterH - tabH - 15, SWP_NOZORDER | SWP_NOMOVE);
		}
	}
}

int TabCtrl_GetItemText(HWND hWnd, int iItem, TCHAR* pszText, int cchTextMax) {
	TCITEM ti;
	ti.pszText = pszText;
	ti.cchTextMax = cchTextMax;
	ti.mask = TCIF_TEXT;
	TabCtrl_GetItem(hWnd, iItem, &ti);

	return (int)_tcslen(pszText);
}

COLORREF RichEdit_GetTextColor (HWND hWnd, int pos) {
	CHARFORMAT cf = {0};
	cf.cbSize = sizeof(CHARFORMAT);
	cf.dwMask = CFM_COLOR;

	SendMessage(hWnd, EM_SETSEL, pos, pos + 1);
	SendMessage(hWnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM) &cf);
	return cf.crTextColor;
}

HTREEITEM TreeView_AddItem (const TCHAR* caption, HTREEITEM parent = TVI_ROOT, int type = 0, int disabled = 0) {
	TVITEM tvi{0};
	TVINSERTSTRUCT tvins{0};
	tvi.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
	tvi.pszText = (TCHAR*)caption;
	tvi.cchTextMax = _tcslen(caption) + 1;
	tvi.lParam = type;

	if (disabled && (type == INDEX || type == TRIGGER)) {
		tvi.stateMask = TVIS_CUT;
		tvi.state = TVIS_CUT;
	}

	tvins.item = tvi;
	tvins.hInsertAfter = parent != TVI_ROOT || type == 0 ? TVI_LAST : abs(type) == 1 ? TVI_FIRST : treeItems[abs(type) - 1];
	tvins.hParent = parent;
	return (HTREEITEM)SendMessage(hTreeWnd, TVM_INSERTITEM, 0, (LPARAM)(LPTVINSERTSTRUCT)&tvins);
};

void updateTree(int type, TCHAR* select) {
	if (type < 0) {
		printf("(updateTree) Error: illegal value of type: %i\n", type);
		return;
	}

	TCHAR selText[256] = {0};
	int selType = 0;
	if (type && select) {
		_stprintf(selText, TEXT("%ls"), select);
		selType = type;
	} else {
		TV_ITEM tv{0};
		tv.mask = TVIF_TEXT | TVIF_PARAM;
		tv.pszText = selText;
		tv.cchTextMax = 255;
		tv.hItem = TreeView_GetSelection(hTreeWnd);
		TreeView_GetItem(hTreeWnd, &tv);
		selType = type && abs(tv.lParam) == type ? type : abs(tv.lParam);
	}

	int yPos = GetScrollPos(hTreeWnd, SB_VERT);

	bool isExpanded = false;
	if (type) {
		TV_ITEM tv{0};
		tv.mask = TVIF_STATE;
		tv.hItem = treeItems[type];
		isExpanded = TreeView_GetItem(hTreeWnd, &tv) && (tv.state & TVIS_EXPANDED);
	}

	TreeView_DeleteItem(hTreeWnd, type == 0 ? TVI_ROOT : treeItems[type]);
	if (type == 0) {
		for (int i = 1; i < 5; i++)
			treeItems[i] = TreeView_AddItem(TYPES16p[i], TVI_ROOT, -i);
	} else {
		treeItems[type] = TreeView_AddItem(TYPES16p[type], TVI_ROOT, -type);
	}

	sqlite3_stmt *stmt;
	char sql[] =
		"select t.name, t.type, " \
		"iif(t.type in ('table', 'view'), group_concat(c.name || iif(t.type = 'table', ': ' || c.type || iif(c.pk,' [pk]',''), ''), ','), null) columns, 0 disabled " \
		"from sqlite_master t left join pragma_table_xinfo c on t.tbl_name = c.arg and c.schema = 'main' " \
		"where t.sql is not null and t.type = coalesce(?1, t.type) and t.name <> 'sqlite_sequence' " \
		"group by t.type, t.name order by t.type, t.name";
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (type)
			sqlite3_bind_text(stmt, 1, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
	else
			sqlite3_bind_null(stmt, 1);

	while (rc == SQLITE_OK && SQLITE_ROW == sqlite3_step(stmt)) {
		TCHAR* name16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 0));
		TCHAR* type16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 1));
		TCHAR* columns16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 2));
		BOOL disabled = sqlite3_column_int(stmt, 3);

		HTREEITEM hItem = 0;
		if (!_tcscmp(type16, TEXT("table")))
			hItem = TreeView_AddItem(name16, treeItems[TABLE], TABLE);

		if (!_tcscmp(type16, TEXT("view")))
			hItem = TreeView_AddItem(name16, treeItems[VIEW], VIEW);

		if (!_tcscmp(type16, TEXT("trigger")))
			hItem = TreeView_AddItem(name16, treeItems[TRIGGER], TRIGGER, disabled);

		if (!_tcscmp(type16, TEXT("index")))
			hItem = TreeView_AddItem(name16, treeItems[INDEX], INDEX, disabled);

		if (!_tcscmp(type16, TEXT("table")) || !_tcscmp(type16, TEXT("view"))) {
			TCHAR* column16 = _tcstok (columns16, TEXT(","));
			while (column16 != NULL) {
				TreeView_AddItem(column16, hItem, !_tcscmp(type16, TEXT("table")) ? COLUMN : 0);
				column16 = _tcstok (NULL, TEXT(","));
			}
		}

		delete [] type16;
		delete [] name16;
		delete [] columns16;
	}
	sqlite3_finalize(stmt);

	if (SQLITE_OK != sqlite3_errcode(db)) {
		sqlite3_prepare_v2(db, "select name, type from sqlite_master where sql is not null and type = coalesce(?1, type) and name <> 'sqlite_sequence' order by 1", -1, &stmt, 0);
		if (type)
				sqlite3_bind_text(stmt, 1, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
		else
				sqlite3_bind_null(stmt, 1);

		while (SQLITE_ROW == sqlite3_step(stmt)) {
			char* type8 = (char*)sqlite3_column_text(stmt, 1);
			int type = !strcmp(type8, TYPES8[TABLE]) ? TABLE :
				!strcmp(type8, TYPES8[VIEW]) ? VIEW :
				!strcmp(type8, TYPES8[INDEX]) ? INDEX :
				!strcmp(type8, TYPES8[TRIGGER]) ? TRIGGER :
				-1;

			if (type == -1)
				continue;

			char* name8 = (char*)sqlite3_column_text(stmt, 0);
			TCHAR* name16 = utils::utf8to16(name8);
			HTREEITEM hItem = TreeView_AddItem(name16, treeItems[type], type);
			delete [] name16;

			if (type == TABLE || type == VIEW) {
				sqlite3_stmt *substmt;
				if (SQLITE_OK == sqlite3_prepare_v2(db, "select name || iif(?2 = 'table', ': ' || type || case when pk then ' [pk]' else '' end, '') from pragma_table_xinfo(?1) order by cid", -1, &substmt, 0)) {
					sqlite3_bind_text(substmt, 1, name8, strlen(name8), SQLITE_TRANSIENT);
					sqlite3_bind_text(substmt, 2, type8, strlen(type8), SQLITE_TRANSIENT);
					while(SQLITE_ROW == sqlite3_step(substmt)) {
						TCHAR* column16 = utils::utf8to16((char *) sqlite3_column_text(substmt, 0));
						TreeView_AddItem(column16, hItem, COLUMN);
						delete [] column16;
					}
				}

				if (SQLITE_DONE != sqlite3_errcode(db)) {
					TCHAR* err16 = utils::utf8to16(sqlite3_errmsg(db));
					TCHAR msg16[256];
					_sntprintf(msg16, 255, TEXT("Error: %ls"), err16);
					delete [] err16;
					TreeView_AddItem(msg16, hItem, 0);
					TreeView_Expand(hTreeWnd, hItem, TVE_EXPAND);
				}
				sqlite3_finalize(substmt);
			}
		}
		sqlite3_finalize(stmt);
	}

	if (type == 0 || type == INDEX || type == TRIGGER) {
		char sql[] = "select d.name, d.type, 1 disabled from disabled d where d.type = coalesce(?1, d.type) and d.dbpath = ?2 order by type, name";
		int rc = sqlite3_prepare_v2(prefs::db, sql, -1, &stmt, 0);
		if (type)
				sqlite3_bind_text(stmt, 1, TYPES8[type], strlen(TYPES8[type]), SQLITE_TRANSIENT);
		else
				sqlite3_bind_null(stmt, 1);

		const char* dbpath = sqlite3_db_filename(db, 0);
		sqlite3_bind_text(stmt, 2, dbpath, strlen(dbpath), SQLITE_TRANSIENT);

		while (rc == SQLITE_OK && SQLITE_ROW == sqlite3_step(stmt)) {
			TCHAR* name16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 0));
			TCHAR* type16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 1));
			BOOL disabled = sqlite3_column_int(stmt, 2);

			if (!_tcscmp(type16, TEXT("trigger")))
				TreeView_AddItem(name16, treeItems[TRIGGER], TRIGGER, disabled);

			if (!_tcscmp(type16, TEXT("index")))
				TreeView_AddItem(name16, treeItems[INDEX], INDEX, disabled);

			delete [] type16;
			delete [] name16;
		}
		sqlite3_finalize(stmt);
	}

	if (isExpanded)
		TreeView_Expand(hTreeWnd, treeItems[type], TVE_EXPAND);

	if (selType && _tcslen(selText) > 0) {
		HTREEITEM hItem = TreeView_GetNextItem(hTreeWnd, treeItems[selType], TVGN_CHILD);
		while (hItem) {
			TCHAR buf[256] = {0};
			TV_ITEM tv{0};
			tv.mask = TVIF_TEXT;
			tv.pszText = buf;
			tv.cchTextMax = 255;
			tv.hItem = hItem;
			TreeView_GetItem(hTreeWnd, &tv);
			if (!_tcscmp(buf, selText)) {
				TreeView_SelectItem(hTreeWnd, hItem);
				hItem = 0;
			} else {
				hItem = TreeView_GetNextItem(hTreeWnd, hItem, TVGN_NEXT);
				if (!hItem)
					TreeView_SelectItem(hTreeWnd, treeItems[selType]);
			}
		}
	}

	if (!type) {
		TreeView_Expand(hTreeWnd, treeItems[TABLE], TVE_EXPAND);
		TreeView_Expand(hTreeWnd, treeItems[VIEW], TVE_EXPAND);
	}

	for (int i = 0; i < MAX_ENTITY_COUNT; i++)
		if (TABLES[i]) {
			delete [] TABLES[i];
			TABLES[i] = 0;
		}

	int tblNo = 0;
	for (int type = 1; type < 3; type++) { // TABLE + VIEW
		HTREEITEM hItem = TreeView_GetChild(hTreeWnd, treeItems[type]);
		while (hItem) {
			TCHAR* name16 = new TCHAR[256]{0};
			TV_ITEM tv = {0};
			tv.mask = TVIF_TEXT;
			tv.hItem = hItem;
			tv.cchTextMax = 256;
			tv.pszText = name16;
			TreeView_GetItem(hTreeWnd, &tv);
			TABLES[tblNo] = name16;
			tblNo++;

			hItem = TreeView_GetNextSibling(hTreeWnd, hItem);
		}
	}

	TABLES[tblNo] = new TCHAR[256]{0};
	_tcscpy(TABLES[tblNo], TEXT("sqlite_master"));
	TABLES[tblNo + 1] = new TCHAR[256]{0};
	_tcscpy(TABLES[tblNo + 1], TEXT("sqlite_sequence"));
	TABLES[tblNo + 2] = new TCHAR[256]{0};
	_tcscpy(TABLES[tblNo + 2], TEXT("dbstat"));

	SetScrollPos(hTreeWnd, SB_VERT, yPos, true);
	InvalidateRect(hTreeWnd, 0, TRUE);
}

void updateTransactionState() {
	int tabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
	int state = tabNo >= 0 ? tabs[tabNo].db && sqlite3_get_autocommit(tabs[tabNo].db) == 0 : cli.db && sqlite3_get_autocommit(cli.db) == 0;
	SendMessage(hStatusWnd, SB_SETTEXT, SB_TRANSACTION, (LPARAM)(transactionStates[state]));
}

bool showDbError(HWND hWnd) {
	TCHAR* err16 = utils::utf8to16(sqlite3_errmsg(db));
	MessageBox(hWnd, err16, NULL, 0);
	delete [] err16;
	return true;
}

TCHAR* getDDL(TCHAR* name16, int type, bool withDrop) {
	TCHAR* res = 0;
	sqlite3_stmt *stmt;
	if (SQLITE_OK == sqlite3_prepare_v2(db, withDrop ?
		"select group_concat(iif(type = ?1 or type = 'table' or type = 'view', " \
		"iif(type = 'table', '/*\n  Warning: The table will be recreated and all data will be lost.\n  Uncomment drop-statement if you are sure.\n*/' || char(10) || '--', '') || 'drop '|| type || ' \"' || name || '\";' || " \
		"char(10) || char(10), '') || sql || ';', char(10) || char(10) || char(10)) " \
		"from sqlite_master where iif(?1 = 'table' or ?1 = 'view', tbl_name, name) = ?2 order by iif(type = 'table' or type = 'view', 'a', type)" :
		"select sql from sqlite_master where type = ?1 and name = ?2", -1, &stmt, 0)) {

		char* name8 = utils::utf16to8(name16);
		sqlite3_bind_text(stmt, 1, TYPES8[type], strlen(TYPES8[type]),  SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, name8, strlen(name8),  SQLITE_TRANSIENT);
		delete [] name8;

		if (SQLITE_ROW == sqlite3_step(stmt))
			res = utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
	}
	sqlite3_finalize(stmt);
	return res;
}

// A ListView with one column has broken sort. So, the first column is a row number
// The first column stores row length in TCHAR without first column and \0. Used for data export.
// lParam of each row is a row number. Used by cbListComparator for sorting
int ListView_SetData(HWND hListWnd, sqlite3_stmt *stmt, bool isRef) {
	int colCount = sqlite3_column_count(stmt);
	HWND hHeader = ListView_GetHeader(hListWnd);

	sqlite3* stmtDb = sqlite3_db_handle(stmt);

	// Find references for each columns and store appropriate sql to row.
	// If there is no the reference then the inserted row is empty
	// Used to show tooltip by Alt + Click by cell
	// I don't want to tangle with memory managment. This listbox will be automatically destroyed when its parent is gone.
	if (isRef) {
		HWND hColumnsWnd = GetDlgItem(hListWnd, IDC_REFLIST);
		ListBox_AddString(hColumnsWnd, TEXT(""));
		char* dbname8 = utils::getFileName(sqlite3_db_filename(db, 0));
		sqlite3_stmt *stmt2;
		if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, "select r.query from refs r where dbname = ?1 and \"schema\" = ?2 and tblname = ?3 and colname = ?4", -1, &stmt2, 0)) {
			for (int i = 0; i < colCount; i++) {
				if (!sqlite3_column_origin_name(stmt, i)) {
					ListBox_AddString(hColumnsWnd, TEXT(""));
					continue;
				}

				sqlite3_bind_text(stmt2, 1, dbname8, -1,  SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt2, 2, sqlite3_column_database_name(stmt, i), -1,  SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt2, 3, sqlite3_column_table_name(stmt, i), -1,  SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt2, 4, sqlite3_column_origin_name(stmt, i), -1,  SQLITE_TRANSIENT);
				if (SQLITE_ROW == sqlite3_step(stmt2)) {
					TCHAR* line16 = utils::utf8to16((char*)sqlite3_column_text(stmt2, 0));
					ListBox_AddString(hColumnsWnd, line16);
					delete [] line16;
				} else {
					ListBox_AddString(hColumnsWnd, TEXT(""));
				}
				sqlite3_reset(stmt2);
			}
		}
		sqlite3_finalize(stmt2);
		delete [] dbname8;
	}

	if (hHeader == NULL || Header_GetItemCount(hHeader) == 0) {
		for (int i = 0; i <= colCount; i++) {
			TCHAR* name16 = utils::utf8to16(i > 0 ? sqlite3_column_name(stmt, i - 1) : "row length");
			LVCOLUMN lvc;
			lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvc.iSubItem = i;
			lvc.pszText = name16;
			lvc.cchTextMax = _tcslen(name16) + 1;
			lvc.cx = i > 0 ? 100 : 0;
			ListView_InsertColumn(hListWnd, i, &lvc);
			delete [] name16;
		}
	}

	ListView_DeleteAllItems(hListWnd);

	bool isStopByLimit = false;
	int rowLimit = prefs::get("row-limit");
	int rowNo = 0;

	byte* datatypes = (byte*)GetProp(hListWnd, TEXT("DATATYPES"));
	if (datatypes)
		delete [] datatypes;
	datatypes = rowLimit ? new byte[colCount * rowLimit + 1000]{0} : 0; // 1000 for new rows
	SetProp(hListWnd, TEXT("DATATYPES"), (HANDLE)datatypes);

	while(sqlite3_step(stmt) == SQLITE_ROW) {
		if (rowLimit > 0 && rowNo == rowLimit) {
			isStopByLimit = true;
			break;
		}

		if (rowNo == 0) {
			for (int i = 1; i <= colCount; i++) {
				int type = sqlite3_column_type(stmt, i - 1);
				LVCOLUMN lvc = {mask: LVCF_FMT};
				lvc.fmt = type == SQLITE_INTEGER || type == SQLITE_FLOAT ? LVCFMT_RIGHT : LVCFMT_LEFT;
				ListView_SetColumn(hListWnd, i, &lvc);
			}
		}

		LVITEM lvi = {0};
		lvi.mask = 0;
		lvi.iSubItem = 0;
		lvi.iItem = rowNo;
		ListView_InsertItem(hListWnd, &lvi);

		int rowLength = 0;
		for (int i = 1; i <= colCount; i++) {
			int colType = sqlite3_column_type(stmt, i - 1);
			TCHAR* value16 = colType == SQLITE_BLOB ? utils::toBlobSize(sqlite3_column_bytes(stmt, i - 1)) :
				utils::utf8to16(colType == SQLITE_NULL ? "" : (char *) sqlite3_column_text(stmt, i - 1));

			if (datatypes)
				datatypes[i + rowNo * colCount] = colType;

			lvi.iSubItem = i;
			lvi.mask = LVIF_TEXT;
			lvi.pszText = value16;

			ListView_SetItem(hListWnd, &lvi);

			rowLength += _tcslen(value16);
			delete [] value16;
		}

		TCHAR rowLength16[64];
		_stprintf(rowLength16, TEXT("%i"), rowLength);
		lvi.mask = LVIF_TEXT | LVIF_PARAM;
		lvi.iSubItem = 0;
		lvi.iItem = rowNo;
		lvi.pszText = rowLength16;
		lvi.cchTextMax = _tcslen(rowLength16) + 1;
		lvi.lParam = rowNo;
		ListView_SetItem(hListWnd, &lvi);

		rowNo++;
	}

	if (SQLITE_DONE != sqlite3_errcode(stmtDb) && !isStopByLimit) {
		char *err8 = (char*)sqlite3_errmsg(stmtDb);
		TCHAR* err16 = utils::utf8to16(err8);
		TCHAR* msg16 = new TCHAR[_tcslen(err16) + 256]{0};
		_stprintf(msg16, TEXT("*** Terminate with error: %ls"), err16);
		LVITEM lvi = {0};
		lvi.mask = LVIF_TEXT;
		lvi.iSubItem = 0;
		lvi.iItem = 0;
		lvi.pszText = msg16;
		lvi.cchTextMax = _tcslen(msg16) + 1;
		ListView_InsertItem(hListWnd, &lvi);

		delete [] err16;
		delete [] msg16;
	}

	ListView_SetExtendedListViewStyle(hListWnd, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_AUTOSIZECOLUMNS | LVS_EX_LABELTIP);
	for (int i = 1; i <= colCount; i++) {
		ListView_SetColumnWidth(hListWnd, i, LVSCW_AUTOSIZE_USEHEADER);
		int w = ListView_GetColumnWidth(hListWnd, i);
		if (w > 200)
			ListView_SetColumnWidth(hListWnd, i, 200);
		if (w > 0 && w < 60)
			ListView_SetColumnWidth(hListWnd, i, 60);
	}

	if ((WNDPROC)GetWindowLong(hListWnd, GWL_WNDPROC) != cbNewListView)
		cbOldListView = (WNDPROC)SetWindowLong(hListWnd, GWL_WNDPROC, (LONG)cbNewListView);

	return isStopByLimit ? -rowNo : rowNo;
}

int ListView_ShowRef(HWND hListWnd, int rowNo, int colNo) {
	if (IsWindowVisible(hTooltipWnd))
		hideTooltip();

	HWND hColumnsWnd = FindWindowEx(hListWnd, 0, WC_LISTBOX, NULL);
	if (!hColumnsWnd)
		return 0;

	TCHAR query16[4000];
	ListBox_GetText(hColumnsWnd, colNo, query16);
	if (!_tcslen(query16))
		return 0;

	TCHAR value16[255];
	ListView_GetItemText(hListWnd, rowNo, colNo, value16, 254);

	char* query8 = utils::utf16to8(query16);
	char* value8 = utils::utf16to8(value16);
	sqlite3_stmt *stmt;
	if (SQLITE_OK == sqlite3_prepare_v2(db, query8, -1, &stmt, 0)) {
		sqlite3_bind_text(stmt, 1, value8, -1, SQLITE_TRANSIENT);
		if (SQLITE_ROW == sqlite3_step(stmt)) {
			char res8[MAX_TEXT_LENGTH] = {0};
			strcpy(res8, (char*)sqlite3_column_text(stmt, 0));
			strupr(res8);
			for (int i = 1; i < sqlite3_column_count(stmt); i++) {
				char line8[512] = {0};
				snprintf(line8, 511, "%s%s: %s", i > 0 ? "\n" : "", (char*)sqlite3_column_name(stmt, i), (char*)sqlite3_column_text(stmt, i));
				strcat(res8, line8);
			}

			TCHAR* res16 = utils::utf8to16(res8);
			POINT p;
			GetCursorPos(&p);
			showTooltip(p.x, p.y + 10, res16);

			delete [] res16;
		}
	} else {
		showDbError(0);
	}
	sqlite3_finalize(stmt);
	delete [] query8;
	delete [] value8;

	return 0;
}

LRESULT onListViewMenu(HWND hListWnd, int rowNo, int colNo, int cmd, bool ignoreLastColumn) {
	auto getRowLength = [hListWnd](int rowNo) {
		TCHAR rowLength[64 + 1]{0};
		LVITEM lvi = {0};
		lvi.mask = LVIF_TEXT;
		lvi.iSubItem = 0;
		lvi.pszText = rowLength;
		lvi.cchTextMax = 64;
		lvi.iItem = rowNo;
		ListView_GetItem(hListWnd, &lvi);
		return _ttoi(rowLength);
	};

	if (cmd == IDM_RESULT_CHART)
		return DialogBoxParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD_CHART), hMainWnd, (DLGPROC)&dialogs::cbDlgChart, (LPARAM)hListWnd);

	if (cmd == IDM_RESULT_COPY_CELL) {
		int rowLength = getRowLength(rowNo);
		TCHAR buf[rowLength + 1];
		ListView_GetItemText(hListWnd, rowNo, colNo, buf, rowLength + 1);
		utils::setClipboardText(buf);
		return true;
	}

	if (cmd != IDM_RESULT_COPY_ROW && cmd != IDM_RESULT_EXPORT)
		return false;

	HWND hHeader =  ListView_GetHeader(hListWnd);
	int colCount = (int)SendMessage(hHeader, HDM_GETITEMCOUNT, 0, 0L) - ignoreLastColumn;
	int rowCount = ListView_GetSelectedCount(hListWnd);
	int searchNext = LVNI_SELECTED;
	if (cmd == IDM_RESULT_EXPORT  && rowCount < 2) {
		rowCount = ListView_GetItemCount(hListWnd);
		searchNext = LVNI_ALL;
	}

	TCHAR path16[MAX_PATH] {0};
	if (cmd == IDM_RESULT_EXPORT) {
		_stprintf(path16, TEXT("result.csv"));
		if (!utils::saveFile(path16, TEXT("CSV files\0*.csv\0All\0*.*\0"), TEXT("csv"), GetParent(hListWnd)))
			return false;

		if (rowCount == prefs::get("row-limit")) {
			int tabNo = SendMessage(hMainTabWnd, WMU_TAB_GET_CURRENT, 0, 0);
			if (!tools::exportCSV(path16, *tabs[tabNo].tabTooltips))
				MessageBox(hMainWnd, TEXT("Error occurred while export to file"), NULL, MB_OK);
			else
				MessageBox(hMainWnd, TEXT("Done"), TEXT("Info"), MB_OK);
			return true;
		}
	}

	bool withHeader = cmd == IDM_RESULT_EXPORT || HIWORD(GetKeyState(VK_SHIFT));
	const TCHAR* delimiter16 = cmd == IDM_RESULT_COPY_ROW ? tools::DELIMITERS[0] : tools::DELIMITERS[prefs::get("csv-export-delimiter")];
	const TCHAR* newLine16 = cmd == IDM_RESULT_COPY_ROW || !prefs::get("csv-export-is-unix-line") ? TEXT("\r\n") : TEXT("\n");

	if (!colCount || !rowCount)
		return false;

	int bufSize = withHeader ? 255 * colCount : 0;
	int currRowNo = -1;
	while((currRowNo = ListView_GetNextItem(hListWnd, currRowNo, searchNext)) != -1)
		bufSize += getRowLength(currRowNo);

	bufSize += (colCount /* delimiters*/ + 64 /* possible quotes */ + 3 /* new line */) * rowCount + 1 /* EOF */;
	TCHAR res16[bufSize]{0};
	TCHAR buf16[MAX_TEXT_LENGTH]{0};

	auto addValue = [](TCHAR* res16, TCHAR* buf16) {
		TCHAR* qvalue16 = utils::replaceAll(buf16, TEXT("\""), TEXT("\"\""));
		if (_tcschr(qvalue16, TEXT(',')) || _tcschr(qvalue16, TEXT('"')) || _tcschr(qvalue16, TEXT('\n'))) {
			TCHAR val16[_tcslen(qvalue16) + 3]{0};
			_stprintf(val16, TEXT("\"%ls\""), qvalue16);
			_tcscat(res16, val16);
		} else {
			_tcscat(res16, buf16);
		}
		delete [] qvalue16;
	};

	if (withHeader) {
		for(int colNo = 1; colNo < colCount; colNo++) {
			Header_GetItemText(hHeader, colNo, buf16, MAX_TEXT_LENGTH);
			addValue(res16, buf16);
			_tcscat(res16, colNo != colCount - 1 ? delimiter16 : newLine16);
		}
	}

	currRowNo = -1;
	while((currRowNo = ListView_GetNextItem(hListWnd, currRowNo, searchNext)) != -1) {
		for(int colNo = 1; colNo < colCount; colNo++) {
			ListView_GetItemText(hListWnd, currRowNo, colNo, buf16, MAX_TEXT_LENGTH);
			addValue(res16, buf16);
			_tcscat(res16, colNo != colCount - 1 ? delimiter16 : newLine16);
		}
	}

	if (cmd == IDM_RESULT_COPY_ROW)
		utils::setClipboardText(res16);

	if (cmd == IDM_RESULT_EXPORT) {
		FILE* f = _tfopen(path16, TEXT("wb"));
		if (f != NULL) {
			char* res8 = utils::utf16to8(res16);
			fprintf(f, res8);
			delete [] res8;
		} else {
			MessageBox(0, TEXT("Error to open file"), NULL, MB_OK);
		}

		fclose(f);
	}

	return true;
}

void updateHighlighting(HWND hWnd) {
	int size = GetWindowTextLength(hWnd);
	TCHAR text[size + 1];
	GetWindowText(hWnd, text, size + 1);

	CHARFORMAT2 cf2;
	ZeroMemory(&cf2, sizeof(CHARFORMAT2));
	cf2.cbSize = sizeof(CHARFORMAT2) ;
	SendMessage(hWnd, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf2);
	cf2.dwMask = CFM_BACKCOLOR | CFM_COLOR | CFM_BOLD;
	cf2.crBackColor = RGB(255, 255, 255);
	cf2.dwEffects = 0;
	cf2.crTextColor = RGB(255, 0, 0);

	int crPos;
	SendMessage(hWnd, EM_GETSEL, (WPARAM)&crPos, (WPARAM)&crPos);

	int pos = 0;
	int rCount = 0;
	int mode = 0; // 0 - casual, 1 - keyword, 2 - function, 3 - pragma, 4 - quotted string, 5 - single line comment, 6 - multiline comment
	COLORREF colors[7] = {
		RGB(0, 0, 0),
		(COLORREF)prefs::get("color-keyword"),
		(COLORREF)prefs::get("color-function"),
		(COLORREF)prefs::get("color-pragma"),
		(COLORREF)prefs::get("color-quoted"),
		(COLORREF)prefs::get("color-comment"),
		(COLORREF)prefs::get("color-comment")
	};
	TCHAR breakers[] = TEXT(" \"'`\n\t-;:(),=<>/");

	while (pos < size) {
		mode =
			text[pos] == TEXT('/') && (pos < size - 1) && text[pos + 1] == TEXT('*') ? 6 :
			text[pos] == TEXT('-') && (pos < size - 1) && text[pos + 1] == TEXT('-') ? 5 :
			text[pos] == TEXT('"') || text[pos] == TEXT('\'') ? 4 :
			_tcschr(breakers, text[pos]) ? 0 :
			1;

		int start = pos;
		int prCount = rCount;

		switch (mode) {
			case 0: {
				rCount += text[pos] == TEXT('\r');
				pos++;
			}
			break;

			case 1:
			case 2:
			case 3: {
				do {
					rCount += text[pos] == TEXT('\r');
					pos++;
				} while (!_tcschr(breakers, text[pos]) && pos < size);

				TCHAR buf[pos - start + 1]{0};
				for(int i = 0; i < pos - start; i++)
					buf[i] = _totlower(text[start + i]);

				TCHAR* tbuf = utils::trim(buf);
				mode = 0;

				if (_tcslen(buf)) {
					for (int i = 0; !mode && SQL_KEYWORDS[i]; i++)
						mode = !_tcscmp(SQL_KEYWORDS[i], tbuf) ? 1 : 0;

					for (int i = 0; !mode && FUNCTIONS[i]; i++)
						mode = !_tcscmp(FUNCTIONS[i], tbuf) ? 2 : 0;

					for (int i = 0; !mode && PRAGMAS[i]; i++)
						mode = !_tcscmp(PRAGMAS[i], tbuf) || (_tcsncmp(tbuf, TEXT("pragma_"), 7) == 0 && _tcscmp(tbuf + 7, PRAGMAS[i]) == 0) ? 3 : 0;
				}

				delete [] tbuf;
			}
			break;

			case 4: {
				TCHAR q = text[start];
				do {
					rCount += text[pos] == TEXT('\r');
					pos++;
				} while (!((text[pos] == q) && (text[pos - 1] != TEXT('\\'))) && pos < size);

				if (text[pos] == q)
					pos++;
			}
			break;

			case 5: {
				while (text[pos] != TEXT('\n') && pos < size) {
					rCount += text[pos] == TEXT('\r');
					pos++;
				}
			}
			break;

			case 6: {
				while (pos < size && (text[pos] != TEXT('*') || text[pos + 1] != TEXT('/'))) {
					rCount += text[pos] == TEXT('\r');
					pos++;
				}

				if (pos < size - 1 && text[pos] == TEXT('*') && text[pos + 1] == TEXT('/'))
					pos += 2;
			}
			break;
		}

		int from = start - prCount;
		int to = pos - rCount;
		from = from < 0 ? 0 : from;
		to = from > to ? from : to;

		cf2.crTextColor = colors[mode];
		cf2.dwEffects = mode == 1 || mode == 2 || mode == 3 ? CFM_BOLD : 0;

		SendMessage(hWnd, EM_SETSEL, from, to);
		SendMessage(hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &cf2);
	}

	SendMessage(hWnd, EM_SETSEL, crPos, crPos);

	currParenthesisPos[0] = -1;
	currParenthesisPos[1] = -1;
}

void updateParenthesisHighlighting(HWND hWnd) {
	// crPos, crLine, etc -  where the cursor is standing
	int crPos;
	SendMessage(hWnd, EM_GETSEL, (WPARAM)&crPos, (WPARAM)&crPos);

	auto setChar = [hWnd](int pos, bool isSelected) {
		CHARFORMAT2 cf;
		ZeroMemory(&cf, sizeof(CHARFORMAT2));
		cf.cbSize = sizeof(CHARFORMAT2);
		cf.dwMask = CFM_BACKCOLOR | CFM_EFFECTS | CFM_BOLD;
		cf.dwEffects = isSelected ? CFM_BOLD : 0;
		cf.crBackColor = isSelected ? prefs::get("color-parenthesis") : RGB(255, 255, 255);
		SendMessage(hWnd, EM_SETSEL, pos, pos + 1);
		SendMessage(hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &cf);
	};

	for (int i = 0;  i < 2 && currParenthesisPos[i] != -1; i++) {
		setChar(currParenthesisPos[i], false);
		currParenthesisPos[i] = -1;
	}

	SendMessage(hWnd, EM_SETSEL, crPos, crPos);
	int lineCount = SendMessage(hWnd, EM_GETLINECOUNT, 0, 0);
	int crLineNo = SendMessage(hWnd, EM_LINEFROMCHAR, -1, 0);
	int crLineSize = SendMessage(hWnd, EM_LINELENGTH, crPos, 0);
	int crLineIdx = SendMessage(hWnd, EM_LINEINDEX, -1, 0);
	TCHAR crLine[crLineSize + 1]{0};
	crLine[0] = crLineSize;
	SendMessage(hWnd, EM_GETLINE, crLineNo, (LPARAM)crLine);
	int pos = crPos - crLineIdx;

	TCHAR parentheses[] = TEXT("[]{}()");
	TCHAR* chr = _tcschr(parentheses, crLine[pos]);

	if (chr && chr[0] && !RichEdit_GetTextColor(hWnd, crLineIdx + pos)) {
		int dir = chr[0] == TEXT('[') || chr[0] == TEXT('{') || chr[0] == TEXT('(') ? +1 : -1;

		setChar(crLineIdx + pos, true);
		currParenthesisPos[0] = crLineIdx + pos;

		int counter = 1;
		int currLineNo = crLineNo;
		while (currLineNo >= 0 && currLineNo < lineCount && currParenthesisPos[1] == -1) {
			int currLineIdx = SendMessage(hWnd, EM_LINEINDEX, currLineNo, 0);
			int currLineSize = SendMessage(hWnd, EM_LINELENGTH, currLineIdx, 0);
			TCHAR currLine[currLineSize + 1]{0};
			currLine[0] = currLineSize;
			SendMessage(hWnd, EM_GETLINE, currLineNo, (LPARAM)currLine);

			pos = currLineNo == crLineNo ? pos + dir : dir > 0 ? 0 : currLineSize - 1;
			while (pos >= 0 && pos < currLineSize && currParenthesisPos[1] == -1) {
				TCHAR* chr2 = _tcschr(parentheses, currLine[pos]);
				if (chr2 && chr2[0] && !RichEdit_GetTextColor(hWnd, currLineIdx + pos)) {
					counter += chr == chr2 ? +1 : chr + dir == chr2 ? -1 : 0;
					if (counter == 0) {
						setChar(currLineIdx + pos, true);
						currParenthesisPos[1] = currLineIdx + pos;
					}
				}
				pos += dir;
			}
			currLineNo += dir;
		}
	}

	SendMessage(hWnd, EM_SETSEL, crPos, crPos);
}

void updateOccurrenceHighlighting(HWND hWnd) {
	int min, max;
	SendMessage(hWnd, EM_GETSEL, (WPARAM)&min, (LPARAM)&max);

	int size = max - min;
	TCHAR selection[size + 1]{0};
	SendMessage(hWnd, EM_GETSELTEXT, size + 1, (LPARAM)selection);

	bool isAlphaNum = true;
	for (int i = 0; isAlphaNum && i < size; i++)
		isAlphaNum = _istalnum(selection[i]) || selection[i] == TEXT('_');

	// Reset all
	CHARFORMAT2 cf2;
	ZeroMemory(&cf2, sizeof(CHARFORMAT2));
	cf2.cbSize = sizeof(CHARFORMAT2);
	cf2.dwMask = CFM_BACKCOLOR;
	cf2.crBackColor = RGB(255, 255, 255);

	SendMessage(hWnd, EM_SETSEL, 0, -1);
	SendMessage(hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &cf2);

	if (size > 1 && isAlphaNum) {
		cf2.crBackColor = RGB(225, 225, 255);

		int tsize = GetWindowTextLength(hWnd);
		TCHAR text[tsize + 1];
		GetWindowText(hWnd, text, tsize + 1);

		TCHAR breakers[] = TEXT(" \"'`\r\n\t-;:(),=<>/");

		int nl = 0;
		for (int i = 0; i < tsize - size; i++) {
			nl += text[i] == TEXT('\n');
			if (_tcsncmp(text + i, selection, size) == 0 &&
				(i == 0 || _tcschr(breakers, text[i - 1]) != NULL) &&
				_tcschr(breakers, text[i + size]) != NULL) {
				SendMessage(hWnd, EM_SETSEL, i - nl, i + size - nl);
				SendMessage(hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &cf2);
				i += size;
			}
		}
	}

	// The selection has a direction defined by min and max positions.
	// The direction must be restored for the selection to work properly.
	// https://docs.microsoft.com/en-us/windows/win32/controls/em-setsel
	// The start value is the anchor point of the selection, and the end value is the active end.
	// If the user uses the SHIFT key to adjust the size of the selection, the active end can move but the anchor point remains the same.
	if (min == max || selStartPos == min) {
		SendMessage(hWnd, EM_SETSEL, min, max);
	} else {
		SendMessage(hWnd, EM_SETSEL, max, min);
	}
}

void processHighlight(HWND hWnd, bool isRequireHighligth, bool isRequireParenthesisHighligth, bool isRequireOccurrenceHighlight) {
	if (!isRequireHighligth && !isRequireParenthesisHighligth && !isRequireOccurrenceHighlight)
		return;

	IUnknown *tr_code = NULL;
	ITextDocument *td_code;

	SendMessage(hWnd, EM_GETOLEINTERFACE, 0, (LPARAM)&tr_code);
	if(tr_code == (IRichEditOle*)NULL) {
		MessageBox(0, TEXT("Error when trying to get RichEdit OLE Object"), NULL, 0);
		return;
	}
	tr_code->QueryInterface(IID_ITextDocument,(void**)&td_code);

	POINT scrollPos{0};
	SendMessage(hWnd, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPos);

	long cnt = 0;
	SendMessage(hWnd, EM_SETEVENTMASK, 0, ENM_NONE);
	td_code->Undo(tomSuspend, NULL);
	td_code->Freeze(&cnt);

	if (isRequireHighligth)
		updateHighlighting(hWnd);

	if (isRequireOccurrenceHighlight)
		updateOccurrenceHighlighting(hWnd);

	if (isRequireParenthesisHighligth)
		updateParenthesisHighlighting(hWnd);

	td_code->Undo(tomResume, NULL);
	td_code->Unfreeze(&cnt);
	td_code->Release();
	tr_code->Release();

	SendMessage(hWnd, EM_SETSCROLLPOS, 0, (LPARAM)&scrollPos);
	InvalidateRect(hWnd, 0, TRUE);
	SendMessage(hWnd, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE |  ENM_KEYEVENTS | ENM_MOUSEEVENTS | ENM_DROPFILES);
}

int ListView_Sort(HWND hListWnd, int colNo) {
	hSortingResultWnd = hListWnd;
	// lParam is used to store tab no, so use window text to store sorting params
	TCHAR buf[32];
	GetWindowText(hListWnd, buf, 32);
	DWORD param = _tcstol(buf, NULL, 10);
	WORD order = LOWORD(param) == colNo ? !HIWORD(param) : 1;
	param = MAKELPARAM(colNo, order);
	ListView_SortItems (hListWnd, cbListComparator, param);
	_ultot(param, buf, 10);
	SetWindowText(hListWnd, buf);

	int rowCount = ListView_GetItemCount(hListWnd);

	byte* datatypes = (byte*)GetProp(hListWnd, TEXT("DATATYPES"));
	if (datatypes != NULL){
		int colCount = Header_GetItemCount(ListView_GetHeader(hListWnd)) - 1;

		byte* datatypes2 = new byte[rowCount * colCount + 1000]{0};
		for (int rowNo = 0; rowNo < rowCount; rowNo++) {
			LVITEM lvi = {0};
			lvi.mask = LVIF_PARAM;
			lvi.iSubItem = 0;
			lvi.iItem = rowNo;
			ListView_GetItem(hListWnd, &lvi);

			for (int colNo = 1; colNo <= colCount; colNo++)
				datatypes2[colNo + rowNo * colCount] = datatypes[colNo + lvi.lParam * colCount];
		}

		delete [] datatypes;
		SetProp(hListWnd, TEXT("DATATYPES"), (HANDLE)datatypes2);
	}

	for (int i = 0; i < rowCount; i++) {
		LVITEM lvi = {0};
		lvi.iItem = i;
		lvi.mask = LVIF_PARAM;
		lvi.iSubItem = 0;
		lvi.lParam = i;
		ListView_SetItem(hListWnd, &lvi);
	}

	return true;
}

int ListView_Reset(HWND hListWnd) {
	ListView_DeleteAllItems(hListWnd);
	HWND hHeader = ListView_GetHeader(hListWnd);
	int cnt = Header_GetItemCount(hHeader);
	for (int i = 0; i < cnt; i++) {
		ListView_DeleteColumn(hListWnd, cnt - 1 - i);
		Header_DeleteItem(hHeader, cnt - 1 - i);
	}

	return true;
}

int ListView_GetColumnCount(HWND hWnd) {
	HWND hHeader = ListView_GetHeader(hWnd);
	return hHeader ? Header_GetItemCount(hHeader) : -1;
}

int Header_GetItemText(HWND hWnd, int i, TCHAR* pszText, int cchTextMax) {
	if (i < 0)
		return FALSE;

	TCHAR buf[cchTextMax]{0};

	HDITEM hdi {0};
	hdi.mask = HDI_TEXT;
	hdi.pszText = buf;
	hdi.cchTextMax = cchTextMax;
	int rc = Header_GetItem(hWnd, i, &hdi);

	_tcsncpy(pszText, buf, cchTextMax);
	return rc;
}

bool search(HWND hWnd) {
	int len = _tcslen(searchString);
	if (!len)
		return false;

	int crPos = LOWORD(SendMessage(hWnd, EM_GETSEL, 0, 0));
	FINDTEXT ft = {{crPos, GetWindowTextLength(hWnd)}, searchString};

	LONG mode = prefs::get("case-sensitive") ? FR_MATCHCASE | FR_DOWN : FR_DOWN;
	int pos = SendMessage(hWnd, EM_FINDTEXT, mode, (LPARAM)&ft);
	if (crPos == pos) {
		ft.chrg.cpMin = crPos + _tcslen(searchString);
		pos = SendMessage(hWnd, EM_FINDTEXT, mode, (LPARAM)&ft);
	}

	if (pos == -1 && crPos != 0) {
		ft.chrg.cpMin = 0;
		pos = SendMessage(hWnd, EM_FINDTEXT, mode, (LPARAM)&ft);
	}

	if (pos != -1) {
		SendMessage(hWnd, EM_SETSEL, pos, pos + len);
	} else {
		MessageBeep(0);
	}

	return pos != -1;
}

bool processEditorEvents(MSGFILTER* pF) {
	HWND hWnd = pF->nmhdr.hwndFrom;
	bool isControl = HIWORD(GetKeyState(VK_CONTROL));
	bool isShift = HIWORD(GetKeyState(VK_SHIFT));

	if (pF->msg == WM_CHAR || pF->msg == WM_KEYDOWN || pF->msg == WM_KEYUP) {
		int key = pF->wParam;
		int vkKey = MapVirtualKey(LOBYTE(HIWORD(pF->lParam)), MAPVK_VSC_TO_VK);
		bool isKeyDown = pF->lParam & (1U << 31);

		if (hWnd == cli.hEditorWnd && ((pF->msg == WM_KEYDOWN && key == VK_UP) || (pF->msg == WM_KEYDOWN && key == VK_DOWN) || (pF->msg == WM_KEYUP && key == VK_TAB)) && isControl) {
			suggestCLIQuery(key);
			return true;
		}

		if (hWnd != hEditorWnd && key == VK_RETURN && isControl) {
			PostMessage(GetParent(hWnd), WM_COMMAND, IDC_DLG_OK, 0);
			pF->wParam = 0;
			return true;
		}

		if (key == 0x56 && isControl && !isKeyDown) { // Ctrl + V Prevent pasting other than text
			pasteText(hWnd);
			pF->wParam = 0;
			return true;
		}

		if (key == 0x5A && isControl && isShift) { // Ctrl + Shift + Z
			if (isKeyDown)
				PostMessage(hWnd, EM_REDO, 0, 0);
			pF->wParam = 0;
			return true;
		}

		if (key == VK_F3 && isShift && isKeyDown) {
			toggleTextCase(hWnd);
			pF->wParam = 0;
			return true;
		}

		if (key == VK_OEM_2 && isControl) { // Ctrl + /
			if (isKeyDown)
				toggleComment(hWnd);
			pF->wParam = 0;
			return true;
		}

		if (key == VK_TAB && !isControl) {
			if (isKeyDown)
				SendMessage(hMainWnd, WM_COMMAND, IDM_PROCESS_TAB, (LPARAM)hWnd);
			pF->wParam = 0;
			return true;
		}

		if (key == 0x57 && isControl) { // Ctrl + W
			if (isKeyDown)
				toggleWordWrap(hWnd);

			pF->wParam = 0;
			return true;
		}

		if ((key == 0x46 || key == 0x52) && isControl) { // Ctrl + F or Ctrl + R
			SendMessage(GetAncestor(hWnd, GA_ROOT), WM_COMMAND, IDM_EDITOR_FIND, 0);
			pF->wParam = 0;
			return true;
		}

		if (key == 0x54 && isControl) { // Ctrl + T
			if (isKeyDown)
				SendMessage(GetAncestor(hWnd, GA_ROOT), WM_COMMAND, IDM_EDITOR_FORMAT, 0);
			pF->wParam = 0;
			return true;
		}

		if (vkKey == VK_F3 && !isKeyDown) {
			search(hWnd);
			pF->wParam = 0;
			return true;
		}

		bool res = processAutoComplete(hWnd, key, isKeyDown);
		if (res)
			pF->wParam = 0; // Stop propagation

		return res;
	}

	if (pF->msg == WM_LBUTTONDOWN && isControl) {
		SetFocus(hWnd);
		POINT p{GET_X_LPARAM(pF->lParam), GET_Y_LPARAM(pF->lParam)};
		int pos = SendMessage(hWnd, EM_CHARFROMPOS, 0, (LPARAM)&p);
		SendMessage(hWnd, EM_SETSEL, pos, pos);
		return SendMessage(hMainWnd, WMU_SHOW_INFO, 0, 0);
	}

	if (pF->msg == WM_LBUTTONDBLCLK)
		doubleClickTick = GetTickCount(); // issue #50

	return 0;
}

bool processAutoComplete(HWND hEditorWnd, int key, bool isKeyDown) {
	// https://stackoverflow.com/questions/8161741/handling-keyboard-input-in-win32-wm-char-or-wm-keydown-wm-keyup
	// Shift + 7 and Shift + 9 equals to up and down
	bool isNavKey = !HIWORD(GetKeyState(VK_SHIFT)) && (key == VK_ESCAPE || key == VK_UP || key == VK_DOWN || key == VK_RETURN);
	bool isCtrlSpace = HIWORD(GetKeyState(VK_CONTROL)) && (key == VK_SPACE);

	if (IsWindowVisible(hAutoComplete) && isNavKey) {
		if (isKeyDown) {
			if (key == VK_ESCAPE)
				ShowWindow(hAutoComplete, SW_HIDE);

			if (key == VK_UP || key == VK_DOWN) {
				int iCount = ListBox_GetCount(hAutoComplete);
				int pos = ListBox_GetCurSel(hAutoComplete);
				ListBox_SetCurSel(hAutoComplete, (pos + (key == VK_UP ? -1 : 1) + iCount) % iCount);
			}

			if (key == VK_RETURN) {
				TCHAR buf[1024]{0}, qbuf[1030]{0};
				int pos = ListBox_GetCurSel(hAutoComplete);
				ListBox_GetText(hAutoComplete, pos, buf);

				bool isAlphaNum = !_istdigit(buf[0]);
				for (int i = 0; isAlphaNum && (i < (int)_tcslen(buf)); i++)
					isAlphaNum = _istalnum(buf[i]) || (buf[i] == TEXT('_'));
				for (int i = 0; TEMPLATES[i] && !isAlphaNum; i++)
					isAlphaNum = _tcsicmp(buf, TEMPLATES[i]) == 0;

				if (!isAlphaNum)
					_stprintf(qbuf, TEXT("\"%ls\""), buf);

				long data = GetWindowLong(hEditorWnd, GWL_USERDATA);
				SendMessage(hEditorWnd, WM_SETREDRAW, FALSE, 0);
				SendMessage(hEditorWnd, EM_SETSEL, LOWORD(data), HIWORD(data)); //-1
				SendMessage(hEditorWnd, EM_REPLACESEL, TRUE, (LPARAM)(isAlphaNum ? buf : qbuf));
				SendMessage(hEditorWnd, WM_SETREDRAW, TRUE, 0);
				ShowWindow(hAutoComplete, SW_HIDE);
			}
		}

		if (!isKeyDown && ListBox_GetCount(hAutoComplete) == 1 && (key == VK_UP || key == VK_DOWN)) {
			ShowWindow(hAutoComplete, SW_HIDE);
			return false;
		}

		return true;
	}

	if (!isCtrlSpace && (!isKeyDown || isNavKey))
		return false;

	if (isCtrlSpace && !isKeyDown)
		return true;

	if (IsWindowVisible(hAutoComplete) && key == VK_CONTROL && isKeyDown)
		return true;

	if (isCtrlSpace && IsWindowVisible(hTooltipWnd))
		return true;

	int crPos;
	SendMessage(hEditorWnd, EM_GETSEL, (WPARAM)&crPos, 0);

	int currLineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, -1, 0);
	int currLineSize = SendMessage(hEditorWnd, EM_LINELENGTH, crPos, 0);
	int currLineIdx = SendMessage(hEditorWnd, EM_LINEINDEX, -1, 0);
	TCHAR currLine[currLineSize + 1]{0};
	currLine[0] = currLineSize;
	SendMessage(hEditorWnd, EM_GETLINE, currLineNo, (LPARAM)currLine);
	size_t start = crPos - currLineIdx;
	size_t end = start;
	TCHAR breakers[] = TEXT(" \"'`\n\r\t+-;:(),=<>!");

	while(start > 0 && !_tcschr(breakers, currLine[start - 1]))
		start--;

	while(end < _tcslen(currLine) && !_tcschr(breakers, currLine[end]))
		end++;

	TCHAR word[end - start + 1] = {0};
	for (size_t i = 0; i < end - start; i++)
		word[i] = currLine[start + i];

	size_t wPos = crPos - currLineIdx - start; // Cursor position in the word

	ListBox_ResetContent(hAutoComplete);
	size_t wLen = _tcslen(word);

	const HWND hWnd = hAutoComplete;
	auto addString = [&word, wLen, hWnd, wPos, isCtrlSpace](const TCHAR* str, bool noEntryCheck = false) {
		if (!_tcsnicmp(str, word, wLen) && _tcslen(str) == wLen)
			return 1;

		if(!(noEntryCheck || (!_tcsnicmp(str, word, wLen) && _tcslen(str) != wLen && (wPos == wLen || isCtrlSpace))))
			return 0;

		TCHAR str2[_tcslen(str) + 1]{0};
		_tcscpy(str2, str);
		if (!noEntryCheck && wLen > 0 && word[0] == _totupper(word[0])) {
			if (wLen == 1 || (wLen > 1 && word[1] == _totupper(word[1])))
				_tcsupr(str2);
			else
				str2[0] = _totupper(str2[0]);
		}
		ListBox_AddString(hWnd, str2);
		return 0;
	};

	int isExact = 0;
	// tablename. or aliasname.
	TCHAR* dotPos = _tcsrchr(word, TEXT('.'));
	if (dotPos && wPos == wLen) {
		TCHAR prefix[_tcslen(word) - _tcslen(dotPos) + 1]{0};
		_tcsncpy(prefix, word, _tcslen(word) - _tcslen(dotPos));
		size_t prefixLen = _tcslen(prefix);

		if (prefixLen > 0) {
			size_t tLen = GetWindowTextLength(hEditorWnd);
			TCHAR text[tLen + 1]{0};
			GetWindowText(hEditorWnd, text, tLen + 1);
			_tcslwr(text);

			for (int i = 0; TABLES[i]; i++)	{
				bool isSuitable = _tcslen(TABLES[i]) == prefixLen && !_tcsicmp(TABLES[i], prefix);

				size_t tbl_aLen = _tcslen(TABLES[i]) + prefixLen;
				TCHAR tbl_a[tbl_aLen + 1 + 1 + 2] {0}; // +1 - space, +1 - \0, +2 for quotes
				TCHAR tbl_t[][10] = {TEXT("%ls %ls"), TEXT("\"%ls\" %ls"), TEXT("'%ls' %ls"), TEXT("[%ls] %ls"), TEXT("`%ls` %ls")};
				for (int j = 0; j < 5 && !isSuitable; j++) {
					_stprintf(tbl_a, tbl_t[j], TABLES[i], prefix); // tablename alias
					_tcslwr(tbl_a);
					TCHAR* p = _tcsstr(text, tbl_a);

					isSuitable = p && // found
						(_tcslen(p) == tLen || _tcschr(breakers, (p - 1)[0])) && // not xxxtablename alias
						(p + tbl_aLen == 0 || _tcschr(breakers, (p + tbl_aLen + (j == 0 ? 1 : 3))[0])); // not tablename aliasxxx
				}

				if (isSuitable) {
					TCHAR query16[512] = {0};
					_stprintf(query16, TEXT("select name from pragma_table_info('%ls') where name like '%ls%%' and name <> '%ls' order by cid"), TABLES[i], dotPos + 1, dotPos + 1);
					char* query8 = utils::utf16to8(query16);

					sqlite3_stmt *stmt;
					sqlite3_prepare_v2(db, query8, -1, &stmt, 0);
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* column16 =  utils::utf8to16((char*)sqlite3_column_text(stmt, 0));
						int pos = ListBox_AddString(hAutoComplete, column16);
						ListBox_SetItemData(hAutoComplete, pos, 0);
						delete [] column16;
					}
					sqlite3_finalize(stmt);

					delete [] query8;
					break;
				}
			}
		}

		sqlite3_stmt *stmt;
		TCHAR query16[512] = {0};
		_stprintf(query16, TEXT("select name from (select name from \"%ls\".sqlite_master where type in ('table', 'view') union select 'sqlite_master') where name like '%ls%%'"), prefix, dotPos + 1);
		char* query8 = utils::utf16to8(query16);
		int rc = sqlite3_prepare_v2(db, query8, -1, &stmt, 0);
		while ((rc == SQLITE_OK) && (SQLITE_ROW == sqlite3_step(stmt))) {
			TCHAR* name16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 0));
			isExact += addString(name16, true);
			delete [] name16;
		}
		sqlite3_finalize(stmt);
		delete [] query8;
	}

	// Columns
	int tLen = GetWindowTextLength(hEditorWnd);
	if (tLen < MAX_TEXT_LENGTH) {
		TCHAR text[tLen + 1];
		GetWindowText(hEditorWnd, text, tLen + 1);
		_tcslwr(text);

		int cPos = crPos;
		// fix new line \r \n
		for (int i = 0; (i < cPos) && (i < tLen + 1); i++)
			if (text[i] == TEXT('\r'))
				cPos = cPos + 1;

		//                    qStart                                    qEnd
		//                    |                                         |
		// where discount > 0;   select id,   nam| from customers where ;
		//                                 |     |
		//                              tPos     cPos

		int tPos = cPos;
		TCHAR breakers[] = TEXT("\"\',\r\n\t)] ");
		while (tPos > 0 && _tcschr(breakers, text[tPos - 1]) == 0)
			tPos--;

		while (tPos > 0 && _istgraph(text[tPos - 1]) == 0)
			tPos--;

		if (wLen || (cPos > 6 && _tcschr(TEXT(" \r\n;"), text[cPos]) != NULL && _tcschr(breakers, text[tPos]) != 0 && tPos != cPos && (
			utils::isStartBy(text, tPos - 1, TEXT(",")) ||
			utils::isStartBy(text, tPos - 1, TEXT("(")) ||
			utils::isStartBy(text, tPos - 1, TEXT(">")) ||
			utils::isStartBy(text, tPos - 1, TEXT("<")) ||
			utils::isStartBy(text, tPos - 1, TEXT("=")) ||
			(utils::isStartBy(text, tPos - 3, TEXT("and")) && _tcschr(breakers, text[tPos - 4]) != 0) ||
			(utils::isStartBy(text, tPos - 2, TEXT("or")) && _tcschr(breakers, text[tPos - 3]) != 0) ||
			(utils::isStartBy(text, tPos - 2, TEXT("on")) && _tcschr(breakers, text[tPos - 3]) != 0) ||
			(utils::isStartBy(text, tPos - 6, TEXT("select")) && (tPos < 7 || _tcschr(breakers, text[tPos - 7]) != 0)) ||
			(utils::isStartBy(text, tPos - 5, TEXT("where")) && _tcschr(breakers, text[tPos - 6]) != 0) ||
			(utils::isStartBy(text, tPos - 8, TEXT("order by"))&& _tcschr(breakers, text[tPos - 9]) != 0) ||
			(utils::isStartBy(text, tPos - 8, TEXT("group by"))&& _tcschr(breakers, text[tPos - 9]) != 0) ||
			(utils::isStartBy(text, tPos - 6, TEXT("having"))&& _tcschr(breakers, text[tPos - 7]) != 0)
		))) {
			int nParentheses = 0;
			int qStart = cPos;
			while (qStart > 0) {
				if (text[qStart] == TEXT(')'))
					nParentheses++;
				if (text[qStart] == TEXT('(') && nParentheses > 0)
					nParentheses--;

				if (nParentheses >= 0 && (utils::isStartBy(text, qStart, TEXT("select")) || text[qStart - 1] == TEXT(';')))
					break;

				qStart--;
			}

			nParentheses = 0;
			int qEnd = cPos;
			while (qEnd < tLen) {
				if (text[qEnd] == TEXT('('))
					nParentheses++;
				if (text[qEnd] == TEXT(')') && nParentheses > 0)
					nParentheses--;

				if (nParentheses >= 0 && (utils::isStartBy(text, qEnd, TEXT("select")) || text[qEnd] == TEXT(';')))
					break;

				qEnd++;
			}

			TCHAR query16[qEnd - qStart + 1]{0};
			_tcsncpy(query16, text + qStart, qEnd - qStart);
			if (_tcslen(query16) > 0) {
				sqlite3_stmt *stmt;
				char sql8[] =
					"select c.name from sqlite_master t left join pragma_table_xinfo c on t.tbl_name = c.arg and c.schema = 'main' " \
					"where t.sql is not null and t.type in ('table', 'view') and ?1 regexp '\\b' || lower(t.tbl_name) || '\\b' order by t.tbl_name, c.cid";

				if (SQLITE_OK == sqlite3_prepare_v2(db, sql8, -1, &stmt, 0)) {
					char* query8 = utils::utf16to8(query16);
					sqlite3_bind_text(stmt, 1, query8, strlen(query8), SQLITE_TRANSIENT);
					delete [] query8;

					bool isNoCheck = wLen == 0;
					while (SQLITE_ROW == sqlite3_step(stmt)) {
						TCHAR* name16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 0));
						isExact += addString(name16, isNoCheck);
						delete [] name16;
					}
				}
				sqlite3_finalize(stmt);
			}
		}
	}

	if (start > 6 && !_tcsnicmp(currLine + start - 7, TEXT("pragma "), 7)) {
		for (int i = 0; PRAGMAS[i]; i++)
			isExact += addString(PRAGMAS[i]);
	} else if (
		((wLen == 0 && _tcschr(TEXT("\r\n\t ("), currLine[start]) != 0) || wLen > 0) &&
		((start > 2 && !_tcsnicmp(currLine + start - 3, TEXT("on "), 3)) ||
		(start > 4 && (
			!_tcsnicmp(currLine + start - 5, TEXT("from "), 5) ||
			!_tcsnicmp(currLine + start - 5, TEXT("join "), 5) ||
			!_tcsnicmp(currLine + start - 5, TEXT("into "), 5)
		)) ||
		(start > 6 && !_tcsnicmp(currLine + start - 7, TEXT("update "), 7)))) {
		bool isNoCheck = wLen == 1 && word[0] == TEXT(' ');
		for (int i = 0; TABLES[i]; i++)
			isExact += addString(TABLES[i], isNoCheck);

		sqlite3_stmt *stmt;
		int rc = sqlite3_prepare_v2(db, "select name from pragma_database_list() union all select name from pragma_module_list where name not like 'pragma_%'", -1, &stmt, 0);
		while ((rc == SQLITE_OK) && (SQLITE_ROW == sqlite3_step(stmt))) {
			TCHAR* name16 = utils::utf8to16((char *) sqlite3_column_text(stmt, 0));
			isExact += addString(name16, isNoCheck);
			delete [] name16;
		}
		sqlite3_finalize(stmt);

		TCHAR buf[255];
		for (int i = 0; PRAGMAS[i]; i++) {
			_stprintf(buf, TEXT("pragma_%ls"), PRAGMAS[i]);
			isExact += addString(buf, isNoCheck);
		}
	} else if (wLen > 1) {
		for (int i = 0; FUNCTIONS[i]; i++)
			isExact += addString(FUNCTIONS[i]);

		for (int i = 0; SQL_KEYWORDS[i]; i++)
			isExact += addString(SQL_KEYWORDS[i]);

		for (int i = 0; TEMPLATES[i]; i++)
			isExact += addString(TEMPLATES[i]);

		for (int i = 0; TABLES[i]; i++)
			isExact += addString(TABLES[i]);
	}

	if (isExact && !isCtrlSpace)
		ListBox_ResetContent(hAutoComplete);

	int h = SendMessage(hAutoComplete, LB_GETITEMHEIGHT, 0, 0);
	int iCount = ListBox_GetCount(hAutoComplete);

	if (iCount) {
		POINT p = {0};
		GetCaretPos(&p);
		ClientToScreen(hEditorWnd, &p);

		SetWindowPos(hAutoComplete, 0, p.x, p.y + 20, 170, MIN(h * iCount + 5, 150), SWP_NOZORDER | SWP_NOACTIVATE);
		SetWindowLong(hAutoComplete, GWL_USERDATA, (LONG)hEditorWnd);
		SetWindowLong(hEditorWnd, GWL_USERDATA, MAKELONG(currLineIdx + (dotPos && _tcslen(dotPos) != wLen ? end - _tcslen(dotPos) + 1 : start), currLineIdx + end));
	}

	ListBox_SetCurSel(hAutoComplete, 0);
	ShowWindow(hAutoComplete, iCount ? SW_SHOW : SW_HIDE);
	SetFocus(hEditorWnd);
	return false;
}

LRESULT CALLBACK cbNewAutoComplete(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_LBUTTONDBLCLK || (msg == WM_KEYUP && wParam == VK_RETURN) || (msg == WM_KEYUP && wParam == VK_ESCAPE)) {
		HWND hEditorWnd = (HWND)GetWindowLong(hWnd, GWL_USERDATA);
		int rc = processAutoComplete(hEditorWnd, wParam == VK_ESCAPE ? VK_ESCAPE : VK_RETURN, true);
		SetFocus(hEditorWnd);
		return rc;
	}

	return CallWindowProc(cbOldAutoComplete, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK cbNewResultTab(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_NOTIFY) {
		NMHDR* pHdr = (LPNMHDR)lParam;
		if (pHdr->code == (UINT)NM_CUSTOMDRAW) {
			int result = CDRF_DODEFAULT;
			HWND hListWnd = pHdr->hwndFrom;
			byte* datatypes = (byte*)GetProp(hListWnd, TEXT("DATATYPES"));
			if (!datatypes)
				return result;

			NMLVCUSTOMDRAW* pCustomDraw = (LPNMLVCUSTOMDRAW)lParam;
			if (pCustomDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
				result = CDRF_NOTIFYITEMDRAW;

			if (pCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
				result = CDRF_NOTIFYSUBITEMDRAW | CDRF_NEWFONT;

			if (pCustomDraw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
				int rowNo = pCustomDraw->nmcd.dwItemSpec;
				int colNo = pCustomDraw->iSubItem;
				int colCount = Header_GetItemCount(ListView_GetHeader(hListWnd)) - 1;
				pCustomDraw->clrTextBk = GRIDCOLORS[datatypes[colNo + colCount * rowNo]]; //ListView_GetColor(datatypes[colNo + colCount * rowNo]);
			}
			return result;
		}
	}
	return CallWindowProc(cbOldResultTab, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK cbNewMainTabRename(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_CHAR && wParam == VK_RETURN)
		return 0;

	if (msg == WM_KEYUP && wParam == VK_ESCAPE) {
		DestroyWindow(hWnd);
		return 0;
	}

	if (processEditKeys(hWnd, msg, wParam, lParam))
		return 0;

	if ((msg == WM_KEYDOWN && wParam == VK_RETURN) || (msg == WM_KILLFOCUS)) {
		HWND hTabWnd = GetParent(hWnd);
		int tabNo = GetWindowLong(hWnd, GWL_USERDATA);
		int len = SendMessage(hWnd, WM_GETTEXTLENGTH, 0, 0);
		TCHAR tabName[len + 1] = {0};
		GetWindowText(hWnd, tabName, len + 1);
		SendMessage(hTabWnd, WMU_TAB_SET_TEXT, tabNo, (LPARAM)tabName);
		DestroyWindow(hWnd);
		return 0;
	}

	return CallWindowProc(cbOldMainTabRenameEdit, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK cbNewMainTab(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	RECT rc = {0};
	GetClientRect(hWnd, &rc);
	SIZE size = {rc.right, rc.bottom};
	const int TAB_WIDTH = 120;
	const int TAB_LENGTH = 17;
	const int CLI_WIDTH = 30;

	TCHAR* headers = (TCHAR*)GetProp(hWnd, TEXT("HEADERS"));
	LONG* styles = (LONG*)GetProp(hWnd, TEXT("STYLES"));
	int currTab = (int)GetProp(hWnd, TEXT("CURRENT"));
	int tabCount = (int)GetProp(hWnd, TEXT("COUNT"));
	LONG clistyle = (LONG)GetProp(hWnd, TEXT("CLISTYLE"));

	switch (msg) {
		case WM_NCDESTROY: {
			RemoveProp(hWnd, TEXT("CURRENT"));
			RemoveProp(hWnd, TEXT("NEXTTABNO"));
			RemoveProp(hWnd, TEXT("COUNT"));
			RemoveProp(hWnd, TEXT("CLISTYLE"));

			TCHAR* headers = (TCHAR*)GetProp(hWnd, TEXT("HEADERS"));
			if (headers)
				delete [] headers;
			RemoveProp(hWnd, TEXT("HEADERS"));

			int* styles = (int*)GetProp(hWnd, TEXT("STYLES"));
			if (styles)
				delete [] styles;
			RemoveProp(hWnd, TEXT("STYLES"));
		}
		break;

		// Used as WM_CREATE
		case WMU_TAB_ADD: {
			if (tabCount == MAX_TAB_COUNT)
				return -1;

			if (!headers) {
				headers = new TCHAR[MAX_TAB_COUNT * TAB_LENGTH]{0};
				SetProp(hWnd, TEXT("HEADERS"), (HANDLE)headers);
			}

			if (!styles) {
				styles = new LONG[MAX_TAB_COUNT]{0};
				SetProp(hWnd, TEXT("STYLES"), (HANDLE)styles);
			}

			tabCount++;
			int no = (int)GetProp(hWnd, TEXT("NEXTTABNO")) + 1;
			_stprintf(headers + (tabCount - 1) * TAB_LENGTH, TEXT("Editor #%i"), no);
			SetProp(hWnd, TEXT("NEXTTABNO"), (HANDLE)no);
			SetProp(hWnd, TEXT("COUNT"), (HANDLE)tabCount);

			NMHDR Hdr = {hWnd, (UINT)GetWindowLong(hWnd, GWL_ID), NM_TAB_ADD};
			SendMessage(GetParent(hWnd), WM_NOTIFY, tabCount - 1, (LPARAM)&Hdr);

			return tabCount - 1;
		}
		break;

		case WMU_TAB_DELETE : {
			int tabNo = wParam;
			if (tabNo < 0 || tabNo > tabCount || tabCount == 1)
				return 0;

			NMHDR Hdr = {hWnd, (UINT)GetWindowLong(hWnd, GWL_ID), NM_TAB_REQUEST_DELETE};
			if(SendMessage(GetParent(hWnd), WM_NOTIFY, tabNo, (LPARAM)&Hdr))
				return 0;

			for (int i = tabNo * TAB_LENGTH; i < (tabCount - 1) * TAB_LENGTH; i++)
				headers[i] = headers[i + TAB_LENGTH];
			for (int i = (tabCount - 1) * TAB_LENGTH; i < MAX_TAB_COUNT * TAB_LENGTH; i++)
				headers[i] = 0;

			for (int i = tabNo; i < tabCount; i++)
				styles[i] = styles[i + 1];

			if (tabNo < currTab)
				SetProp(hWnd, TEXT("CURRENT"), (HANDLE)(currTab - 1));
			SetProp(hWnd, TEXT("COUNT"), (HANDLE)(tabCount - 1));

			Hdr = {hWnd, (UINT)GetWindowLong(hWnd, GWL_ID), NM_TAB_DELETE};
			SendMessage(GetParent(hWnd), WM_NOTIFY, tabNo, (LPARAM)&Hdr);

			return 1;
		}
		break;

		case WMU_TAB_SET_TEXT: {
			int tabNo = wParam;
			int tabCount = SendMessage(hWnd, WMU_TAB_GET_COUNT, 0, 0);
			TCHAR* text = (TCHAR*)lParam;

			if (tabNo < 0 || tabNo >= tabCount)
				return 1;

			int len = text ? _tcslen(text) : 0;
			for (int i = 0; i < TAB_LENGTH; i++)
				headers[tabNo * TAB_LENGTH + i] = i < len ? text[i] : 0;

			SendMessage(hWnd, WM_PAINT, 0, 0);
		}
		break;

		case WMU_TAB_SET_STYLE: {
			int tabNo = wParam;

			if (tabNo < -2 || tabNo >= tabCount)
				return 1;

			if (tabNo == -1)
				SetProp(hWnd, TEXT("CLISTYLE"), (HANDLE)lParam);
			else
				styles[tabNo] = lParam;

			SendMessage(hWnd, WM_PAINT, 0, 0);
		}
		break;

		case WMU_TAB_GET_STYLE: {
			int tabNo = wParam;
			return tabNo == -1 ? clistyle : tabNo < 0 || tabNo >= tabCount ? 0 : styles[tabNo];
		}
		break;

		case WMU_TAB_GET_TEXT: {
			int tabNo = wParam;
			TCHAR* buf = (TCHAR*)lParam;

			if (tabNo < 0 || tabNo >= tabCount || !buf)
				return 1;

			for (int i = 0; i < TAB_LENGTH; i++)
				buf[i] = headers[tabNo * TAB_LENGTH + i];
			buf[TAB_LENGTH - 1] = 0;

			SendMessage(hWnd, WM_PAINT, 0, 0);
		}
		break;

		case WMU_TAB_SET_CURRENT: {
			if ((currTab == (int)wParam) && (lParam != 0))
				return 0;

			SetProp(hMainTabWnd, TEXT("CURRENT"), (HANDLE)wParam);
			SendMessage(hWnd, WM_PAINT, 0, 0);

			NMHDR Hdr = {hWnd, (UINT)GetWindowLong(hWnd, GWL_ID), NM_TAB_CHANGE};
			SendMessage(GetParent(hWnd), WM_NOTIFY, wParam, (LPARAM)&Hdr);

			return 1;
		}

		case WMU_TAB_GET_COUNT: {
			return tabCount;
		}

		case WMU_TAB_GET_CURRENT: {
			return currTab;
		}

		case WM_ERASEBKGND: {
			RECT rc{0};
			GetClientRect(hWnd, &rc);
			HBRUSH hBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
			FillRect((HDC)wParam, &rc, hBrush);
			DeleteObject(hBrush);

			return 1;
		}
		break;

		case WM_PAINT: {
			InvalidateRect(hWnd, NULL, true);

			PAINTSTRUCT ps{0};
			ps.fErase = true;
			HDC hDC = BeginPaint(hWnd, &ps);

			HFONT hOldFont = (HFONT)SelectObject(hDC, hDefFont);
			HPEN hCrossPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
			HPEN hBorderPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_GRAYTEXT));
			HPEN hUnborderPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNFACE));

			// CLI
			if (currTab == -1) {
				RECT rc = {0, 0, CLI_WIDTH, size.cy};
				HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
				FillRect(hDC, &rc, hBrush);
				DeleteObject(hBrush);
			}
			SetBkColor(hDC, currTab == -1 ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNFACE));
			SetTextColor(hDC, clistyle ? RGB(255, 0, 0) : RGB(0, 0, 0));
			TextOut(hDC, 10, 3, TEXT(">_"), 2);

			// borders
			SelectObject(hDC, hBorderPen);
			MoveToEx(hDC, CLI_WIDTH, 0, NULL);
			LineTo(hDC, 0, 0);
			LineTo(hDC, 0, size.cy - 1);
			if (currTab == -1)
				SelectObject(hDC, hUnborderPen);
			LineTo(hDC, CLI_WIDTH, size.cy - 1);

			// Bottom line
			SelectObject(hDC, hBorderPen);
			MoveToEx(hDC, CLI_WIDTH, rc.bottom - 1, NULL);
			LineTo(hDC, CLI_WIDTH + rc.right - 1, rc.bottom - 1);

			for (int tabNo = 0; tabNo < tabCount; tabNo++) {
				if (tabNo == currTab) {
					RECT rc = {CLI_WIDTH + tabNo * TAB_WIDTH, 0, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH, size.cy};
					HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
					FillRect(hDC, &rc, hBrush);
					DeleteObject(hBrush);
				}

				SetTextColor(hDC, styles[tabNo] ? RGB(255, 0, 0) : GetSysColor(COLOR_BTNTEXT));
				SetBkColor(hDC, tabNo == currTab ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNFACE));
				int pos = tabNo * TAB_LENGTH;
				int len = 0;
				for (len = 0; len < TAB_LENGTH && (headers[pos + len] != 0); len++);
				TextOut(hDC, CLI_WIDTH + 5 + tabNo * TAB_WIDTH, 3, (TCHAR*)headers + pos, len);

				if (tabNo == 0) {
					SelectObject(hDC, hBorderPen);
					MoveToEx(hDC, CLI_WIDTH, 0, NULL);
					LineTo(hDC, CLI_WIDTH, size.cy - 1);
				}

				// right border
				SelectObject(hDC, hBorderPen);
				MoveToEx(hDC, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH - 1, 0, NULL);
				LineTo(hDC, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH - 1, size.cy - 1);

				// Top
				SelectObject(hDC, hBorderPen);
				MoveToEx(hDC, CLI_WIDTH + tabNo * TAB_WIDTH, 0, NULL);
				LineTo(hDC, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH, 0);

				// Unborder bottom
				if (tabNo == currTab) {
					SelectObject(hDC, hUnborderPen);
					MoveToEx(hDC, CLI_WIDTH + tabNo * TAB_WIDTH, size.cy - 1, NULL);
					LineTo(hDC, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH - 1, size.cy - 1);
				}

				// Cross
				if (tabCount > 1) {
					SelectObject(hDC, hCrossPen);
					MoveToEx(hDC, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH - 16, 4, NULL);
					LineTo(hDC, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH - 7, 13);
					MoveToEx(hDC, CLI_WIDTH + (tabNo + 1) * TAB_WIDTH - 16, 13, NULL);
					LineTo(hDC, CLI_WIDTH + (tabNo + 1 ) * TAB_WIDTH - 7, 4);
				}
			}

			// Plus
			SelectObject(hDC, hCrossPen);
			MoveToEx(hDC, CLI_WIDTH + tabCount * TAB_WIDTH + 5, 9, NULL);
			LineTo(hDC, CLI_WIDTH + tabCount * TAB_WIDTH + 14, 9);
			MoveToEx(hDC, CLI_WIDTH + tabCount * TAB_WIDTH + 10, 4, NULL);
			LineTo(hDC, CLI_WIDTH + tabCount * TAB_WIDTH + 10, 13);

			DeleteObject(hCrossPen);
			DeleteObject(hBorderPen);
			DeleteObject(hUnborderPen);

			SelectObject(hDC, hOldFont);

			EndPaint(hWnd, &ps);
		}
		break;

		case WM_LBUTTONDOWN: {
			POINT p = {LOWORD(lParam), HIWORD(lParam)};
			int tabNo = (p.x - CLI_WIDTH)/ TAB_WIDTH;
			int tabCount = SendMessage(hWnd, WMU_TAB_GET_COUNT, 0, 0);
			if (p.x < CLI_WIDTH) {
				SendMessage(hWnd, WMU_TAB_SET_CURRENT, -1, 1);
			} else if (tabNo < tabCount) {
				if (p.x > CLI_WIDTH + (tabNo + 1)* TAB_WIDTH - 20) {
					if (SendMessage(hWnd, WMU_TAB_DELETE, tabNo, 0)) {
						if (tabNo == currTab)
							SendMessage(hWnd, WMU_TAB_SET_CURRENT, tabNo == tabCount - 1 ? tabNo - 1 : tabNo, 0);

						SendMessage(hWnd, WM_PAINT, 0, 0);
					}
				} else {
					SendMessage(hWnd, WMU_TAB_SET_CURRENT, tabNo, 1);
				}

				SetProp(hWnd, TEXT("ISMOUSEDOWN"), (HWND)1);
				SetCapture(hWnd);
			} else if (p.x < CLI_WIDTH + tabNo * TAB_WIDTH + 20) {
				SendMessage(hWnd, WMU_TAB_ADD, 0, 0);
			}

			return 0;
		}
		break;

		case WM_LBUTTONUP: {
			ReleaseCapture();
			RemoveProp(hWnd, TEXT("ISMOUSEDOWN"));
		}
		break;

		case WM_MOUSEMOVE: {
			if (wParam != MK_LBUTTON || !GetProp(hWnd, TEXT("ISMOUSEDOWN")))
				return 0;

			DWORD x = GET_X_LPARAM(lParam);

			int tabNo = (x - CLI_WIDTH)/ TAB_WIDTH;
			if (x <= CLI_WIDTH || tabNo > tabCount - 1 || currTab == tabNo)
				return 0;

			for (int i = 0; i < TAB_LENGTH; i++) {
				TCHAR c = headers[tabNo * TAB_LENGTH + i];
				headers[tabNo * TAB_LENGTH + i] = headers[currTab * TAB_LENGTH + i];
				headers[currTab * TAB_LENGTH + i] = c;
			}

			TEditorTab* tabA = &tabs[tabNo];
			TEditorTab* tabB = &tabs[currTab];
			TEditorTab temp = *tabA;
			*tabA = *tabB;
			*tabB = temp;

			SendMessage(hMainTabWnd, WMU_TAB_SET_CURRENT, tabNo, 0);
		}
		break;

		case WM_MBUTTONDOWN: {
			POINT p = {LOWORD(lParam), HIWORD(lParam)};
			int tabNo = (p.x - CLI_WIDTH)/ TAB_WIDTH;
			int tabCount = SendMessage(hWnd, WMU_TAB_GET_COUNT, 0, 0);
			if (p.x > CLI_WIDTH && tabNo < tabCount && SendMessage(hWnd, WMU_TAB_DELETE, tabNo, 0)) {
				if (tabNo == currTab)
					SendMessage(hWnd, WMU_TAB_SET_CURRENT, tabNo == tabCount - 1 ? tabNo - 1 : tabNo, 0);

				SendMessage(hWnd, WM_PAINT, 0, 0);
			}
			return 0;
		}
		break;

		case WM_LBUTTONDBLCLK: {
			if (wParam == MK_LBUTTON) {
				POINT p = {LOWORD(lParam), HIWORD(lParam)};
				int tabNo = (p.x - CLI_WIDTH)/ TAB_WIDTH;
				int tabCount = SendMessage(hWnd, WMU_TAB_GET_COUNT, 0, 0);

				if (p.x > CLI_WIDTH && tabNo < tabCount) {
					TCHAR tabName[TAB_LENGTH] = {0};
					for (int i = 0; i < TAB_LENGTH; i++)
						tabName[i] = headers[tabNo * TAB_LENGTH + i];
					HWND hEdit = CreateWindow(WC_EDIT, tabName, WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | WS_TABSTOP,
						CLI_WIDTH + tabNo * TAB_WIDTH, 0, TAB_WIDTH, 18, hWnd, (HMENU)IDC_TAB_EDIT, GetModuleHandle(0), 0);
					SendMessage(hEdit, WM_SETFONT, (LPARAM)hDefFont, true);
					SetFocus(hEdit);
					int len = _tcslen(tabName);
					SendMessage(hEdit, EM_SETSEL, (WPARAM)len, (WPARAM)len);
					SetWindowLong(hEdit, GWL_USERDATA, tabNo);
					cbOldMainTabRenameEdit = (WNDPROC)SetWindowLong(hEdit, GWL_WNDPROC, (LPARAM)cbNewMainTabRename);
				}
			}
		}
		break;

		default:
			return CallWindowProc(cbOldMainTab, hWnd, msg, wParam, lParam);
	}
	return 0;
}

// Returns
// * selected text if the editor has selection
// * word under cursor if text out of quotes and full quoted string otherwise
TCHAR* getCurrentText(HWND hWnd) {
	int crStart, crEnd;
	SendMessage(hWnd, EM_GETSEL, (WPARAM)&crStart, (LPARAM)&crEnd);

	TCHAR* text = 0;
	if (crStart == crEnd) {
		int currLineNo = SendMessage(hWnd, EM_LINEFROMCHAR, -1, 0);
		int currLineSize = SendMessage(hWnd, EM_LINELENGTH, crStart, 0);
		int currLineIdx = SendMessage(hWnd, EM_LINEINDEX, -1, 0);
		TCHAR currLine[currLineSize + 1]{0};
		currLine[0]  = currLineSize;

		SendMessage(hWnd, EM_GETLINE, currLineNo, (LPARAM)currLine);
		size_t start = crStart - currLineIdx;
		size_t end = start;

		CHARFORMAT cf{0};
		cf.cbSize = sizeof(CHARFORMAT);
		cf.dwMask = CFM_COLOR;
		SendMessage(hWnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

		bool isQuoted = (COLORREF)prefs::get("color-quoted") == cf.crTextColor;
		if (isQuoted) {
			TCHAR* q = 0;
			int pos = start;
			while (pos >= 0 && (q = _tcschr(TEXT("\"'`["), currLine[pos])) == 0)
				pos--;

			TCHAR* qEnd = q != 0 ? _tcschr(currLine + pos + 1, q[0] == TEXT('[') ? TEXT(']') : q[0]) : 0;
			if (qEnd) {
				int len = _tcslen(currLine);
				start =  pos + 1;
				end = len - _tcslen(qEnd);
			}

			isQuoted = start != end;
		}

		if (!isQuoted) {
			while (start > 0 && (_istalnum(currLine[start - 1]) || currLine[start - 1] == TEXT('_')))
				start--;

			while (end < _tcslen(currLine) && (_istalnum(currLine[end]) || currLine[end] == TEXT('_')))
				end++;
		}

		end = MAX(start, end);

		text = new TCHAR[end - start + 1]{0};
		for (size_t i = 0; i < end - start; i++)
			text[i] = currLine[start + i];
	} else {
		text = new TCHAR[crEnd - crStart + 1]{0};
		TEXTRANGE tr{{crStart, crEnd}, text};
		SendMessage(hWnd, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
	}

	return text;
}

HWND openDialog(int IDD, DLGPROC proc, LPARAM lParam) {
	int no = 0;
	while (hDialogs[no])
		no++;

	if (no >= MAX_DIALOG_COUNT) {
		MessageBox(hMainWnd, TEXT("Max dialog count (32) is exceeded"), 0, 0);
		return 0;
	}

	HWND hDlg = CreateDialogParam(GetModuleHandle(0), MAKEINTRESOURCE(IDD), hMainWnd, proc, lParam);
	hDialogs[no] = hDlg;
	ShowWindow(hDlg, SW_SHOW);
	if (GetAncestor(GetFocus(), GA_ROOT) != hDlg)
		SetFocus(hDlg);

	RECT rc;
	GetWindowRect(hDlg, &rc);
	SetWindowPos(hDlg, 0, rc.left + no * 10, rc.top + no * 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

	return hDlg;
}

bool toggleWordWrap(HWND hEditorWnd) {
	bool isWordWrap = true;
	for (int i = 0; i < MAX_TAB_COUNT + MAX_DIALOG_COUNT; i++) {
		hEditors[i] = IsWindow(hEditors[i]) ? hEditors[i] : 0;
		if (hEditors[i] == hEditorWnd) {
			isWordWrap = false;
			hEditors[i] = 0;
		}
	}

	for (int i = 0; isWordWrap && (i < MAX_TAB_COUNT + MAX_DIALOG_COUNT); i++) {
		if (hEditors[i] == 0) {
			hEditors[i] = hEditorWnd;
			break;
		}
	}

	SendMessage(hEditorWnd, EM_SETTARGETDEVICE,(WPARAM)NULL, !isWordWrap);
	return isWordWrap;
}

bool toggleTextCase (HWND hEditorWnd) {
	SendMessage(hEditorWnd, WM_SETREDRAW, false, 0);
	CHARRANGE range;
	SendMessage(hEditorWnd, EM_EXGETSEL, 0, (LPARAM)&range);

	int caretPos = -1;
	if (range.cpMin == range.cpMax) {
		caretPos = range.cpMin;
		int lineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, range.cpMin, 0);
		int start = SendMessage(hEditorWnd, EM_LINEINDEX, lineNo, 0);
		int len = SendMessage(hEditorWnd, EM_LINELENGTH, start, 0);
		TCHAR line[len + 3]{0};
		line[0] = len + 1;
		SendMessage(hEditorWnd, EM_GETLINE, lineNo, (WPARAM)line);

		TCHAR breakers[] = TEXT(" \"'`\n\t-;:()[],=<>/");

		for (int pos = range.cpMin - start; (pos > 0) && (_tcschr(breakers, line[pos - 1]) == 0); pos--)
			range.cpMin--;

		for (int pos = range.cpMax - start; (pos < len) && (_tcschr(breakers, line[pos]) == 0); pos++)
			range.cpMax++;
		//
	}
	SendMessage(hEditorWnd, EM_SETSEL, range.cpMin, range.cpMax);

	int size = range.cpMax - range.cpMin;
	TCHAR text[size + 1]{0};
	SendMessage(hEditorWnd, EM_GETSELTEXT, size + 1, (LPARAM)text);
	if (text[0] == _totupper(text[0]))
		_tcslwr(text);
	else
		_tcsupr(text);

	SendMessage(hEditorWnd, EM_REPLACESEL, true, (WPARAM)text);
	SendMessage(hEditorWnd, WM_SETREDRAW, true, 0);

	if (caretPos != -1)
		range = {caretPos, caretPos};
	PostMessage(hEditorWnd, EM_SETSEL, range.cpMin, range.cpMax);
	return true;
}

bool toggleComment (HWND hEditorWnd) {
	SendMessage(hEditorWnd, WM_SETREDRAW, false, 0);

	CHARRANGE range;
	SendMessage(hEditorWnd, EM_EXGETSEL, 0, (LPARAM)&range);

	auto getPos = [](TCHAR* line) {
		int res = -1;
		for (int i = 0; i < (int)_tcslen(line); i++) {
			if (line[i] != TEXT('\t') && line[i] != TEXT(' ') && line[i] != TEXT('-'))
				break;

			if (line[i] == TEXT('-') && line[i + 1] == TEXT('-')) {
				res = i;
			}
		}

		return res;
	};

	int opComment = -1;

	int lineStartNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, range.cpMin, 0);
	int lineEndNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, range.cpMax, 0);

	//Fix a bug for the last char in a line
	if ((lineStartNo != lineEndNo) && SendMessage(hEditorWnd, EM_LINEINDEX, lineEndNo, 0) >= range.cpMax)
		lineEndNo--;

	for (int lineNo = lineStartNo; lineNo <= lineEndNo; lineNo++) {
		int start = SendMessage(hEditorWnd, EM_LINEINDEX, lineNo, 0);
		int len = SendMessage(hEditorWnd, EM_LINELENGTH, start, 0);
		TCHAR line[len + 3]{0}; // \r \n \0
		line[0] = len + 1;
		SendMessage(hEditorWnd, EM_GETLINE, lineNo, (WPARAM)line);

		bool isEmpty = true;
		for (int i = 0; isEmpty && (i < len); i++)
			isEmpty = isEmpty && (line[i] == TEXT(' ') || line[i] == TEXT('\t'));
		if (isEmpty)
			continue;

		int commentPos = getPos(line);
		// The operation is defined by first non empty line
		if (opComment == -1)
			opComment = commentPos == -1;

		if (opComment && (commentPos == -1)) {
			bool isEmptyLine = true;
			for (int i = 0; (i < len) && isEmptyLine; i++)
				isEmptyLine = line[i] == TEXT(' ') || line[i] == TEXT('\t');

			if (isEmptyLine)
				continue;

			TCHAR cLine[len + 3 + 3]{0};
			_stprintf(cLine, TEXT("-- %ls"), line);
			SendMessage(hEditorWnd, EM_SETSEL, start, start + len + 1);
			SendMessage(hEditorWnd, EM_REPLACESEL, true, (WPARAM)cLine);

		}

		if (!opComment && (commentPos != -1)) {
			SendMessage(hEditorWnd, EM_SETSEL, start + commentPos, start + len + 1);
			bool isSpace = line[commentPos + 2] == TEXT(' ');
			SendMessage(hEditorWnd, EM_REPLACESEL, true, (WPARAM)(line + commentPos + 2 + isSpace));
		}
	}

	int start = 0, end = 0;
	if (lineStartNo == lineEndNo) {
		start = MAX(range.cpMin + (opComment ?  3 : -3), SendMessage(hEditorWnd, EM_LINEINDEX, lineStartNo, 0));
		end = start;
	} else {
		start = SendMessage(hEditorWnd, EM_LINEINDEX, lineStartNo, 0);
		end = SendMessage(hEditorWnd, EM_LINEINDEX, lineEndNo, 0);
		end += SendMessage(hEditorWnd, EM_LINELENGTH, end, 0);
	}

	SendMessage(hEditorWnd, WM_SETREDRAW, true, 0);
	InvalidateRect(hEditorWnd, NULL, true);

	PostMessage(hEditorWnd, EM_SETSEL, start, end);

	return true;
}

bool pasteText (HWND hEditorWnd) {
	TCHAR* txt = utils::getClipboardText();
	if (_tcslen(txt) > 0)
		SendMessage(hEditorWnd, EM_REPLACESEL, true, (LPARAM)txt);
	delete [] txt;

	return true;
}

void switchDialog(HWND hDlg, bool isNext) {
	int i, j;
	for (i = 0; i < MAX_DIALOG_COUNT && (hDialogs[i] != hDlg); i++);
	if (isNext) {
		for (j = i + 1; j <= MAX_DIALOG_COUNT && (hDialogs[j] == 0); j++);
		if (j == MAX_DIALOG_COUNT)
			for (j = 0; j <= i && (hDialogs[j] == 0); j++);
	} else {
		for (j = i - 1; j != -1 && (hDialogs[j] == 0); j--);
		if (j == -1)
			for (j = MAX_DIALOG_COUNT - 1; j >= i && (hDialogs[j] == 0); j--);
	}

	if (i != j)
		SetForegroundWindow(hDialogs[j]);
}

void saveQuery(const char* storage, const char* query) {
	char sql[256];
	sprintf(sql, "replace into %s (query, time) values (?1, ?2)", storage);

	sqlite3_stmt* stmt;
	if (SQLITE_OK == sqlite3_prepare_v2(prefs::db, sql, -1, &stmt, 0)) {
		sqlite3_bind_text(stmt, 1, query, strlen(query), SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 2, std::time(0));
		sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
}

// Used for Edit controls
// Impelements Ctr + A/Backspace/Del shortcuts
bool processEditKeys(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	bool isControl = HIWORD(GetKeyState(VK_CONTROL));

	// Prevent Beep
	if (msg == WM_CHAR && isControl) {
		int vkKey = MapVirtualKey(LOBYTE(HIWORD(lParam)), MAPVK_VSC_TO_VK);
		return vkKey == 0x41 || vkKey == VK_BACK || vkKey == VK_DELETE;
	}

	if (msg == WM_KEYDOWN && wParam == 0x41 && isControl) { // Ctrl + A
		SendMessage(hWnd, EM_SETSEL, 0, -1);
		return true;
	}

	if (msg == WM_KEYDOWN && (wParam == VK_BACK || wParam == VK_DELETE) && isControl) {
		int size = GetWindowTextLength(hWnd);
		TCHAR text[size + 1]{0};
		GetWindowText(hWnd, text, size + 1);

		int start = 0, end = 0;
		SendMessage(hWnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);

		if (start != end) {
			SendMessage(hWnd, EM_REPLACESEL, true, (LPARAM)TEXT(""));
		} else {
			int pos = start;

			if (wParam == VK_BACK) {
				bool isSpace = pos > 0 && text[pos - 1] == TEXT(' ');
				while ((pos > 0 && text[pos - 1] != TEXT(' ')) || isSpace) {
					isSpace = isSpace && text[pos - 1] == TEXT(' ');
					pos--;
				}

				for (int i = pos; i < size - start + pos; i++)
					text[i] = text[start - pos + i];
				text[size - start + pos] = 0;
			}

			if (wParam == VK_DELETE) {
				for (; pos < size && text[pos] != TEXT(' '); pos++);
				if (pos == start && start < size)
					pos = start + 1;

				for (int i = start; i < size + start - pos; i++)
					text[i] = text[pos - start + i];
				text[size + start - pos] = 0;
				pos = start;
			}

			SetWindowText(hWnd, text);
			SendMessage(hWnd, EM_SETSEL, pos, pos);
		}
		return true;
	}

	return false;
}

unsigned int __stdcall checkUpdate (void* data) {
	time_t t = time(0);
	tm* now = localtime(&t);
	int date = (now->tm_year + 1900) * 10000 + (now->tm_mon + 1) * 100 + now->tm_mday;
	if (date != prefs::get("last-update-check")) {
		DWORD read;
		char buf8[1001]{0};

		char uri[] = "api.github.com";
		char path[] = "repos/little-brother/sqlite-gui/commits/master";
		char headers[] = "Accept: application/vnd.github.v3+json";

		HINTERNET hInet = InternetOpenA("Mozilla/4.0 (compatible; MSIE 6.0b; Windows NT 5.0; .NET CLR 1.0.2914)", INTERNET_OPEN_TYPE_PRECONFIG, "", "", 0);
		HINTERNET hSession = InternetConnectA(hInet, uri, INTERNET_DEFAULT_HTTPS_PORT, "", "", INTERNET_SERVICE_HTTP, 0, 1u);
		HINTERNET hRequest = HttpOpenRequestA(hSession, "GET", path, NULL, uri, 0, INTERNET_FLAG_SECURE, 1);
		if (HttpSendRequestA(hRequest, headers, strlen(headers), 0, 0)) {
			InternetReadFile(hRequest, &buf8, 1000, &read);

			TCHAR* buf16 = utils::utf8to16(buf8);
			TCHAR* message = _tcsstr(buf16, TEXT("\"message\""));
			TCHAR* tree = _tcsstr(buf16, TEXT("\"tree\""));

			if (message && _tcsstr(message, TEXT(GUI_VERSION)) == 0) {
				TCHAR msg[1000]{0};
				_tcsncpy(msg, message + 11, _tcslen(message) - _tcslen(tree) - 13);
				TCHAR* msg2 = utils::replaceAll(msg, TEXT("\\n"), TEXT("\n"));
				_stprintf(msg, TEXT("%ls\n\nWould you like to download it?"), msg2);
				if (IDYES == MessageBox(hMainWnd, msg, TEXT("New version was released"), MB_YESNO))
					ShellExecute(0, 0, TEXT("https://github.com/little-brother/sqlite-gui/releases/latest"), 0, 0 , SW_SHOW);
				delete [] msg2;
			}
			delete [] buf16;
		}
		InternetCloseHandle(hRequest);
		InternetCloseHandle(hSession);
		InternetCloseHandle(hInet);

		prefs::set("last-update-check", date);
	}

	return 1;
}

// Fix issue #50: Double-click broken for text in 'single quotes'
void fixQuoteSelection(HWND hEditorWnd, SELCHANGE* pSc) {
	if ((GetTickCount() - doubleClickTick < 100)) {
		int lineNo = SendMessage(hEditorWnd, EM_LINEFROMCHAR, pSc->chrg.cpMin, 0);
		int lineIdx = SendMessage(hEditorWnd, EM_LINEINDEX, lineNo, 0);
		int lineSize = SendMessage(hEditorWnd, EM_LINELENGTH, lineIdx, 0);
		TCHAR line[lineSize + 1]{0};
		line[0] = lineSize;
		SendMessage(hEditorWnd, EM_GETLINE, lineNo, (LPARAM)line);
		int pos;
		bool isBug = line[pSc->chrg.cpMin - lineIdx] == TEXT('\'');

		for (pos = pSc->chrg.cpMin - lineIdx + 1; isBug && (pos < pSc->chrg.cpMax - lineIdx); pos++)
			isBug = isBug && line[pos] == TEXT(' ');

		if (isBug) {
			for (pos = pSc->chrg.cpMin - lineIdx - 1; pos >= 0 && line[pos] != TEXT('\''); pos--);
			SendMessage(hEditorWnd, EM_SETSEL, lineIdx + pos + 1, pSc->chrg.cpMin);
		}
	}
}

bool saveResultToTable(const TCHAR* table16, int tabNo, int resultNo, int searchNext) {
	HWND hTabWnd = tabs[tabNo].hTabWnd;
	HWND hListWnd = GetDlgItem(hTabWnd, IDC_TAB_ROWS + resultNo);
	HWND hHeader = ListView_GetHeader(hListWnd);
	int colCount = ListView_GetColumnCount(hListWnd);
	int rowCount = searchNext == LVNI_ALL ? ListView_GetItemCount(hListWnd) : ListView_GetSelectedCount(hListWnd);

	bool rc = true;

	TCHAR* schema16 = utils::getName(table16, true);
	TCHAR* tablename16 = utils::getName(table16, false);

	if (rowCount == prefs::get("row-limit")) {
		TCHAR query16[_tcslen(tabs[tabNo].tabTooltips[resultNo]) + 512];
		_stprintf(query16, TEXT("create table \"%ls\".\"%ls\" as %ls"), schema16, tablename16, tabs[tabNo].tabTooltips[resultNo]);
		char* query8 = utils::utf16to8(query16);
		rc = sqlite3_exec(db, query8, 0, 0, 0) == SQLITE_OK;
		delete [] query8;
	} else {
		byte* datatypes = (byte*)GetProp(hListWnd, TEXT("DATATYPES"));

		char create8[colCount * 255 + 1024]{0};
		char insert8[colCount * 255 + 1024]{0};

		char* schema8 = utils::utf16to8(schema16);
		char* tablename8 = utils::utf16to8(tablename16);
		sprintf(create8, "create table \"%s\".\"%s\" (", schema8, tablename8);
		sprintf(insert8, "insert into \"%s\".\"%s\" (", schema8, tablename8);
		delete [] schema8;
		delete [] tablename8;

		for (int colNo = 1; colNo < colCount; colNo++) {
			TCHAR colname16[255];
			Header_GetItemText(hHeader, colNo, colname16, 255);

			char* colname8 = utils::utf16to8(colname16);
			strcat(create8, "\"");
			strcat(create8, colname8);
			strcat(create8, "\"");

			strcat(insert8, "\"");
			strcat(insert8, colname8);
			strcat(insert8, "\"");
			delete [] colname8;

			if (datatypes) {
				strcat(create8,
					datatypes[colNo] == SQLITE_FLOAT ? " real" :
					datatypes[colNo] == SQLITE_INTEGER ? " integer" :
					datatypes[colNo] == SQLITE_TEXT ? " text" :
					""
				);
			}

			if (colNo != colCount - 1) {
				strcat(create8, ", ");
				strcat(insert8, ", ");
			}
		}
		strcat(create8, ")");

		char placeholders8[(colCount  - 1) * 2]{0}; // count = 3 => ?, ?, ?
		for (int i = 0; i < (colCount - 1) * 2 - 1; i++)
			placeholders8[i] = i % 2 ? ',' : '?';

		strcat(insert8, ") values (");
		strcat(insert8, placeholders8);
		strcat(insert8, ")");

		rc = sqlite3_exec(db, "begin", 0, 0, 0) == SQLITE_OK;
		rc = rc && (sqlite3_exec(db, create8, 0, 0, 0) == SQLITE_OK);
		if (rc) {
			sqlite3_stmt *stmt;
			if (SQLITE_OK == sqlite3_prepare_v2(db, insert8, -1, &stmt, 0)) {
				int rowNo = -1;

				while(rc && (rowNo = ListView_GetNextItem(hListWnd, rowNo, searchNext)) != -1) {
					for(int colNo = 1; colNo < colCount; colNo++) {
						TCHAR val16[MAX_TEXT_LENGTH + 1];
						ListView_GetItemText(hListWnd, rowNo, colNo, val16, MAX_TEXT_LENGTH);
						char* val8 = utils::utf16to8(val16);
						utils::sqlite3_bind_variant(stmt, colNo, val8, datatypes && datatypes[colNo] == SQLITE_TEXT);
						delete [] val8;
					}

					rc = sqlite3_step(stmt) == SQLITE_DONE;
					sqlite3_reset(stmt);
				}
			}
			sqlite3_finalize(stmt);
		}
	}

	if (!rc)
		showDbError(hMainWnd);

	delete [] schema16;
	delete [] tablename16;

	sqlite3_exec(db, rc ? "commit" : "rollback", 0, 0, 0);

	return rc;
}

bool startHttpServer() {
	if (!prefs::get("http-server"))
		return false;

	sqlite3* _db;
	if (openConnection(&_db, sqlite3_db_filename(db, 0)))
		http::start(prefs::get("http-server-port"), _db, prefs::get("http-server-debug"));

	return true;
}

bool stopHttpServer() {
	return http::stop();
}

void createTooltip(HWND hWnd) {
	if (hTooltipWnd)
		return;

	hTooltipWnd = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_BALLOON, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWnd, NULL, GetModuleHandle(0), NULL);
	TOOLINFO ti{0};
	ti.cbSize = sizeof(ti);
	ti.hwnd = hWnd;
	ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS |  TTF_TRACK;
	ti.uId = (UINT_PTR)hWnd;

	SendMessage(hTooltipWnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
	SendMessage(hTooltipWnd, TTM_SETMAXTIPWIDTH, 0, 400);
}

void showTooltip(int x, int y, TCHAR* text16) {
	TOOLINFO ti{0};
	ti.cbSize = sizeof(ti);
	SendMessage(hTooltipWnd, TTM_ENUMTOOLS, 0, (LPARAM)&ti);

	ti.lpszText = text16;
	SendMessage(hTooltipWnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
	SendMessage(hTooltipWnd, TTM_TRACKPOSITION, 0, (LPARAM)(DWORD) MAKELONG(x, y));
	SendMessage(hTooltipWnd, TTM_TRACKACTIVATE, true, (LPARAM)(LPTOOLINFO) &ti);
}

void hideTooltip() {
	SendMessage(hTooltipWnd, TTM_TRACKACTIVATE, false, 0);
}

/* Simple query formatter */
//  0 - casual, 1 - -- comment, 2 - / * comment * /, 3 - 'quoted', 4 - "quoted", 5 - `quoted`
int getTextMode(TCHAR c, TCHAR cn, TCHAR cp, int mode, int len) {
	return c == TEXT('\'') && mode == 0 ? 3 :
		c == TEXT('\'') && c != cn  && mode == 3 ? 0 :
		c == TEXT('"') && mode == 0 ? 4 :
		c == TEXT('"') && c != cp && mode == 4 && len > 1 ? 0 :
		c == TEXT('"') && c == cp && mode == 4 && len == 1 ? 0 :
		c == TEXT('`') && mode == 0 ? 5 :
		c == TEXT('`') && mode == 5 ? 0 :
		c == TEXT('-') && cn == TEXT('-') && mode == 0 ? 1 :
		c == TEXT('\n') && mode == 1 ? 0 :
		c == TEXT('/') && cn == TEXT('*') && mode == 0 ? 2 :
		c == TEXT('*') && cn == TEXT('/') && mode == 2 ? 0 :
		mode;
}

int processBlock (int startPos, int startIndent, const TCHAR* input, int* indents, bool* newlines) {
	int pos = startPos;
	int currIndent = startIndent;
	int mode = 0;
	int len = 0;

	bool isStatement =
		utils::isStartBy(input, pos, TEXT("select")) ||
		utils::isStartBy(input, pos, TEXT("insert into")) ||
		utils::isStartBy(input, pos, TEXT("delete from"));

	bool isWith = utils::isStartBy(input, pos, TEXT("with"));

	while (pos < (int)_tcslen(input)) {
		if (mode == 0) {
			if (input[pos] == TEXT('(')) {
				if (isWith) {
					newlines[pos] = true;
					indents[pos] = startIndent - 1;
					currIndent = startIndent - 1;
				}
				pos += processBlock(pos + 1, currIndent + 2, input, indents, newlines) + 1;
				currIndent = startIndent;
				continue;
			}

			if (input[pos] == TEXT(')')) {
				break;
			}

			if (isWith && (input[pos - 2] == ',' || utils::isStartBy(input, pos, TEXT("select")))) {
				newlines[pos] = true;
				indents[pos] = startIndent;
				currIndent = startIndent;
			}

			// 6 - strlen + 1
			bool isValues = pos > 6 && utils::isStartBy(input, pos - 6, TEXT("values"));
			int indent =
				utils::isStartBy(input, pos, TEXT("delete from")) ? 1 :
				utils::isStartBy(input, pos, TEXT("values")) ? 1 :
				isValues ? 7 :
				!utils::isPrecedeBy(input, pos, TEXT("delete")) && utils::isStartBy(input, pos, TEXT("from")) ? 3 :
				utils::isStartBy(input, pos, TEXT("inner join")) ? 3 :
				utils::isStartBy(input, pos, TEXT("left join")) ? 3 :
				!utils::isPrecedeBy(input, pos, TEXT("left")) && !utils::isPrecedeBy(input, pos, TEXT("inner")) && utils::isStartBy(input, pos, TEXT("join")) ? 3 :
				utils::isStartBy(input, pos, TEXT("where")) ? 2 :
				utils::isStartBy(input, pos, TEXT("order by")) ? 2 :
				utils::isStartBy(input, pos, TEXT("group by")) ? 2 :
				utils::isStartBy(input, pos, TEXT("having")) ? 1 :
				utils::isStartBy(input, pos, TEXT("limit")) ? 2 :
				utils::isStartBy(input, pos, TEXT("set")) ? 4 :
				pos > 0 && input[pos - 1] == TEXT(',') && isStatement ? (utils::isStartBy(input, pos + 1, TEXT("--")) || utils::isStartBy(input, pos + 1, TEXT("/*")) ? 1 : 7) :
				pos > 0 && input[pos - 1] == TEXT(';') ? 1 :
				0;

			if (indent > 0) {
				indent--;
				isStatement = (pos > 0 && input[pos - 1] == TEXT(',')) || isValues;
				indents[pos] = pos > 0 && input[pos - 1] == TEXT(';') ? 0 : indent + startIndent;
				newlines[pos] = pos > 0 && input[pos - 1] != TEXT('\n') && !utils::isStartBy(input, pos + 1, TEXT("--")) && !utils::isStartBy(input, pos + 1, TEXT("/*"));
			}
			currIndent = indent > 0 ? startIndent + indent : currIndent + 1;
		} else {
			currIndent++;
		}

		int pmode = mode;
		mode = getTextMode(input[pos], input[pos + 1], pos > 0 ? input[pos - 1] : 0, mode, len);
		len = mode == pmode ? len + 1 : 1;

		pos++;
	}

	return pos - startPos + 1;
}

bool formatQuery (HWND hEditorWnd) {
	CHARRANGE range;
	SendMessage(hEditorWnd, EM_EXGETSEL, 0, (LPARAM)&range);

	bool isSelection = range.cpMin != range.cpMax;
	int size =  isSelection ? range.cpMax - range.cpMin + 1 : GetWindowTextLength(hEditorWnd);
	if (size <= 0)
		return false;

	TCHAR sql[size + 1]{0};
	if (!SendMessage(hEditorWnd, isSelection ? EM_GETSELTEXT : WM_GETTEXT, size + 1, (LPARAM)sql))
		return false;

	TCHAR buf[size + 1]{0};
	int mode = 0;
	int pos = 0;
	int bPos = 0;
	int len = 0;

	// Minify
	while (pos < (int)_tcslen(sql)) {
		TCHAR c = sql[pos];
		TCHAR cn = sql[pos + 1];
		TCHAR cp = bPos > 0 ? buf[bPos - 1] : 0;

		if (mode == 0) {
			if (_tcschr(TEXT(" \t\r\n"), c) == 0) {
				buf[bPos] = c;
				bPos++;
			}

			if (_tcschr(TEXT("\t\r\n"), c) != 0 && _istalpha(cn) && _istalnum(cp)) {
				buf[bPos] = ' ';
				bPos++;
			}

			if (c == ' ' && _istalnum(cn) && _istalnum(cp)) {
				buf[bPos] = TEXT(' ');
				bPos++;
			}
		} else {
			buf[bPos] = c;
			bPos++;
		}

		int pmode = mode;
		mode = getTextMode(c, cn, cp, mode, len);
		len = mode == pmode ? len + 1 : 1;
		pos++;
	}

	// Add spaces
	pos = 0;
	bPos = 0;
	mode = 0;
	TCHAR buf2[MAX_TEXT_LENGTH]{0};
	while (pos < (int)_tcslen(buf)) {
		TCHAR c = buf[pos];
		TCHAR cn = buf[pos + 1];
		TCHAR cp = bPos > 0 ? buf2[bPos - 1] : 0;

		if (mode == 0 && cp != TEXT(' ') && c != cp && c != TEXT(';') && (
			_tcschr(TEXT("(<!*+"), c) != 0 ||
			(_tcschr(TEXT("'\"`"), c) != 0 && cp != TEXT('(')) ||
			(c == TEXT('>') && cp != TEXT('<')) ||
			(c == TEXT('=') && cp != TEXT('!')) ||
			(c == TEXT('/') && cp != TEXT('*')) ||
			(c == TEXT('-') && cp != TEXT('-'))
			)) {

			bool isFunc = false;
			if (c == TEXT('(')) {
				int spos = pos - 1;
				while (spos > 0 && _istalnum(buf[spos]))
					spos--;
				TCHAR fname[pos - 1 - spos + 1]{0};
				_tcsncpy(fname, buf + spos + 1, pos - spos - 1);
				_tcslwr(fname);
				for (int i = 0; !isFunc && FUNCTIONS[i]; i++)
					isFunc = _tcscmp(fname, FUNCTIONS[i]) == 0;
			}

			if (!isFunc) {
				buf2[bPos] = ' ';
				bPos++;
			}
		}

		buf2[bPos] = c;
		bPos++;

		if ((mode == 0 && strchr("=>,*+", c) != 0) ||
			(mode == 0 && c == TEXT(')') && cn != TEXT(',')) ||
			(mode == 0 && c == TEXT('<') && cn != TEXT('>')) ||
			(mode == 0 && c == TEXT('!') && cn != TEXT('=')) ||
			(mode == 0 && c == TEXT('/') && cn != TEXT('*')) ||
			(mode == 0 && c == TEXT('-') && cn != TEXT('-')) ||
			(c != cn && _tcschr(TEXT("),;"), cn) == 0 &&
				((mode == 3 && c == TEXT('\'')) ||
				(mode == 4 && c == TEXT('"')) ||
				(mode == 5 && c == TEXT('`'))))
			) {
			buf2[bPos] = TEXT(' ');
			bPos++;
		}

		pos++;

		int pmode = mode;
		mode = getTextMode(c, cn, cp, mode, len);
		len = mode == pmode ? len + 1 : 1;
	}

	// change keyword/function case
	int keywordCase = prefs::get("format-keyword-case");
	int functionCase = prefs::get("format-function-case");
	if (keywordCase || functionCase) {
		pos = 0;
		mode = 0;
		len = 0;
		while (pos < (int)_tcslen(buf2)) {
			TCHAR c = buf2[pos];
			TCHAR cn = buf2[pos + 1];
			TCHAR cp = pos > 0 ? buf2[pos - 1] : 0;

			if (mode == 0 && (pos == 0 || (pos > 0 && !_istalpha(cp)))) {
				int epos = pos;
				while (epos < (int)_tcslen(buf2) && _istalnum(buf2[epos]))
					epos++;

				if (epos != pos) {
					TCHAR word[epos - pos + 1]{0};
					_tcsncpy(word, buf2 + pos, epos - pos);
					_tcslwr(word);

					for (int i = 0; keywordCase && SQL_KEYWORDS[i]; i++) {
						if ((int)_tcscmp(word, SQL_KEYWORDS[i]) == 0) {
							for (int j = 0; j < (int)_tcslen(word); j++)
								buf2[pos + j] = keywordCase == 1 ? _totlower(buf2[pos + j]) : _totupper(buf2[pos + j]);
							break;
						}
					}

					for (int i = 0; functionCase && FUNCTIONS[i]; i++) {
						if (_tcscmp(word, FUNCTIONS[i]) == 0) {
							for (int j = 0; j < (int)_tcslen(word); j++)
								buf2[pos + j] = keywordCase == 1 ? _totlower(buf2[pos + j]) : _totupper(buf2[pos + j]);
							break;
						}
					}
				}
			}

			pos++;
			int pmode = mode;
			mode = getTextMode(c, cn, cp, mode, len);
			len = mode == pmode ? len + 1 : 1;
		}
	}

	// Format
	pos = 0;
	bPos = 0;

	// calculate indents
	int indents[_tcslen(buf2)]{0};
	bool newlines[_tcslen(buf2)]{0};
	processBlock(0, 0, buf2, indents, newlines);

	TCHAR buf3[_tcslen(buf2) * 10 + 1]{0};
	for (int pos = 0; pos < (int)_tcslen(buf2); pos++) {
		if (newlines[pos]) {
			buf3[bPos] = TEXT('\n');
			bPos++;
		}

		for (int i = 0; i < indents[pos]; i++) {
			buf3[bPos] = TEXT(' ');
			bPos++;
		}

		buf3[bPos] = buf2[pos];
		bPos++;
	}

	SetWindowRedraw(hEditorWnd, false);
	if (!isSelection)
		SendMessage(hEditorWnd, EM_SETSEL, 0, -1);

	SendMessage(hEditorWnd, EM_REPLACESEL, true, (LPARAM)buf3);
	SendMessage(hEditorWnd, EM_SETSEL, range.cpMin, range.cpMin);
	SetWindowRedraw(hEditorWnd, true);

	return true;
}
