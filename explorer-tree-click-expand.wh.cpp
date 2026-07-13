// ==WindhawkMod==
// @id              explorer-tree-click-expand
// @name            Expand folder on click (Explorer nav pane)
// @description     Left click expands a folder one level; Ctrl+click collapses the whole tree; Alt+click expands all subfolders
// @version         1.0
// @author          Didi
// @github          https://github.com/diegoalejo15
// @include         explorer.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Expand folder on click

By default, in File Explorer's navigation pane (tree view), left-clicking
a folder only selects it; you have to click the small chevron/arrow to
expand it.

This mod adds three click behaviors to the navigation pane tree:

- **Left click** on a folder's icon or label: expands it one level (like
  pressing Numpad +). The native chevron behavior is untouched.
- **Ctrl + left click** on any folder: collapses the entire tree back to
  its original state (all folders collapsed).
- **Alt + left click** on a folder: expands it and all its subfolders
  recursively, down to the last level (like pressing the Numpad * key).

Both Ctrl+click and Alt+click behaviors can be individually enabled or
disabled in the mod's settings. Both are enabled by default.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- ctrlClickCollapseAll: true
  $name: Ctrl+Click to collapse all
  $description: >-
    When enabled, Ctrl + left click on any folder in the navigation pane
    collapses the entire tree back to its original state.
- altClickExpandAll: true
  $name: Alt+Click to expand all subfolders
  $description: >-
    When enabled, Alt + left click on a folder in the navigation pane
    expands it and all its subfolders recursively (same as pressing the
    Numpad * key).
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

HHOOK g_hMouseHook = nullptr;
HWND g_hHiddenWnd = nullptr;

bool g_settingCtrlClickCollapseAll = true;
bool g_settingAltClickExpandAll = true;

constexpr UINT WM_APP_COLLAPSE_ALL = WM_APP + 1;
constexpr UINT WM_APP_EXPAND_ALL = WM_APP + 2;
constexpr wchar_t kHiddenWndClass[] = L"ExplorerTreeClickExpandHiddenWnd";

void CollapseAll(HWND hwndTree, HTREEITEM hItem) {
    while (hItem) {
        HTREEITEM hChild = TreeView_GetChild(hwndTree, hItem);
        if (hChild) {
            CollapseAll(hwndTree, hChild);
        }
        TreeView_Expand(hwndTree, hItem, TVE_COLLAPSE);
        hItem = TreeView_GetNextSibling(hwndTree, hItem);
    }
}

// Collapses everything in the tree except the very top-level root item
// (e.g. "Desktop"), which stays expanded so its direct children (Home,
// Gallery, Dropbox, etc.) remain visible - matching the tree's original
// out-of-the-box state instead of hiding everything.
void CollapseAllKeepingRoot(HWND hwndTree) {
    HTREEITEM hRoot = TreeView_GetRoot(hwndTree);
    if (!hRoot) return;

    TreeView_Expand(hwndTree, hRoot, TVE_EXPAND);
    CollapseAll(hwndTree, TreeView_GetChild(hwndTree, hRoot));
}

void ExpandAll(HWND hwndTree, HTREEITEM hItem) {
    // Select the item first - the native "*" (Numpad Multiply) behavior
    // expands the currently selected/focused item and all its descendants,
    // and it's implemented internally in a fast, non-blocking way (unlike
    // manually walking the tree with TVM_EXPAND calls).
    TreeView_SelectItem(hwndTree, hItem);
    SendMessage(hwndTree, WM_KEYDOWN, VK_MULTIPLY, 0);
    SendMessage(hwndTree, WM_KEYUP, VK_MULTIPLY, 0);
}

void LoadSettings() {
    g_settingCtrlClickCollapseAll = Wh_GetIntSetting(L"ctrlClickCollapseAll") != 0;
    g_settingAltClickExpandAll = Wh_GetIntSetting(L"altClickExpandAll") != 0;
}

LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP_COLLAPSE_ALL) {
        HWND hTreeWnd = (HWND)wParam;
        if (hTreeWnd && IsWindow(hTreeWnd)) {
            CollapseAllKeepingRoot(hTreeWnd);
        }
        return 0;
    }
    if (msg == WM_APP_EXPAND_ALL) {
        HWND hTreeWnd = (HWND)wParam;
        HTREEITEM hItem = (HTREEITEM)lParam;
        if (hTreeWnd && IsWindow(hTreeWnd) && hItem) {
            ExpandAll(hTreeWnd, hItem);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN) {
        MSLLHOOKSTRUCT* info = (MSLLHOOKSTRUCT*)lParam;
        HWND hWnd = WindowFromPoint(info->pt);

        wchar_t className[64];
        if (hWnd && GetClassNameW(hWnd, className, ARRAYSIZE(className)) &&
            wcscmp(className, L"SysTreeView32") == 0) {

            POINT clientPt = info->pt;
            ScreenToClient(hWnd, &clientPt);

            TVHITTESTINFO hitTest{};
            hitTest.pt = clientPt;
            HTREEITEM hItem = (HTREEITEM)SendMessage(hWnd, TVM_HITTEST, 0, (LPARAM)&hitTest);

            // Only act if the click landed on the icon or label, NOT on the
            // expand/collapse chevron (that one already toggles natively).
            bool onItemBody = (hitTest.flags & (TVHT_ONITEMICON | TVHT_ONITEMLABEL)) != 0;

            if (hItem && onItemBody) {
                bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

                if (ctrlDown && g_settingCtrlClickCollapseAll) {
                    PostMessage(g_hHiddenWnd, WM_APP_COLLAPSE_ALL, (WPARAM)hWnd, 0);
                } else if (altDown && g_settingAltClickExpandAll) {
                    PostMessage(g_hHiddenWnd, WM_APP_EXPAND_ALL, (WPARAM)hWnd, (LPARAM)hItem);
                } else if (!ctrlDown && !altDown) {
                    TVITEM item{};
                    item.mask = TVIF_STATE | TVIF_CHILDREN;
                    item.hItem = hItem;
                    item.stateMask = TVIS_EXPANDED;
                    SendMessage(hWnd, TVM_GETITEM, 0, (LPARAM)&item);

                    bool hasChildren = item.cChildren != 0;
                    bool isExpanded = (item.state & TVIS_EXPANDED) != 0;

                    if (hasChildren && !isExpanded) {
                        // Post (non-blocking) so we don't delay the system's
                        // mouse hook chain.
                        PostMessage(hWnd, TVM_EXPAND, TVE_EXPAND, (LPARAM)hItem);
                    }
                }
            }
        }
    }

    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

BOOL Wh_ModInit() {
    LoadSettings();

    WNDCLASSW wc{};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kHiddenWndClass;
    RegisterClassW(&wc);

    g_hHiddenWnd = CreateWindowW(
        kHiddenWndClass, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr
    );

    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(nullptr), 0);
    return g_hMouseHook != nullptr;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}

void Wh_ModUninit() {
    if (g_hMouseHook) {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = nullptr;
    }
    if (g_hHiddenWnd) {
        DestroyWindow(g_hHiddenWnd);
        g_hHiddenWnd = nullptr;
    }
    UnregisterClassW(kHiddenWndClass, GetModuleHandle(nullptr));
}
