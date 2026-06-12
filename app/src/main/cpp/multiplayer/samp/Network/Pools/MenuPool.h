#pragma once

#include <cstdint>

class ImGuiRenderer;

#ifndef MAX_MENUS
#define MAX_MENUS 128
#endif

#ifndef MAX_MENU_ITEMS
#define MAX_MENU_ITEMS 12
#endif

#ifndef MAX_MENU_COLUMNS
#define MAX_MENU_COLUMNS 2
#endif

#ifndef MAX_MENU_LINE
#define MAX_MENU_LINE 32
#endif

#ifndef INVALID_MENU_ID
#define INVALID_MENU_ID 255
#endif

#pragma pack(push, 1)
struct MENU_INT
{
    bool bMenu;
    bool bRow[MAX_MENU_ITEMS];
};
#pragma pack(pop)

class CMenu
{
public:
    CMenu(const char* title, float x, float y, uint8_t columns,
          float col1Width, float col2Width, const MENU_INT* interaction);
    ~CMenu() = default;

    void AddMenuItem(uint8_t column, uint8_t row, const char* text);
    void SetColumnTitle(uint8_t column, const char* text);

    void Show();
    void Hide();
    bool IsVisible() const { return m_bVisible; }

    const char* GetMenuItem(uint8_t column, uint8_t row) const;
    const char* GetMenuTitle() const { return m_szTitle; }
    const char* GetMenuHeader(uint8_t column) const;

    uint8_t GetSelectedRow() const { return m_byteSelectedRow; }
    void SetSelectedRow(uint8_t row);
    bool IsRowEnabled(uint8_t row) const;
    bool CanSelect() const { return m_menuInteraction.bMenu; }

    void Render(ImGuiRenderer* renderer);
    bool OnTouchEvent(int type, int x, int y);

private:
    void CopyMenuText(char* dst, const char* src, size_t dstSize);
    float ScaleX(float value) const;
    float ScaleY(float value) const;
    float ColumnWidth(uint8_t column) const;
    float MenuWidth() const;
    float MenuHeight() const;
    int HitTestRow(float x, float y) const;

private:
    char m_szTitle[MAX_MENU_LINE];
    char m_szItems[MAX_MENU_ITEMS][MAX_MENU_COLUMNS][MAX_MENU_LINE];
    char m_szHeader[MAX_MENU_COLUMNS][MAX_MENU_LINE];

    float m_fXPos;
    float m_fYPos;
    float m_fCol1Width;
    float m_fCol2Width;
    uint8_t m_byteColumns;
    uint8_t m_byteSelectedRow;
    bool m_bVisible;
    MENU_INT m_menuInteraction;
};

class CMenuPool
{
public:
    CMenuPool();
    ~CMenuPool();

    CMenu* New(uint8_t menuId, const char* title, float x, float y, uint8_t columns,
               float col1Width, float col2Width, const MENU_INT* interaction);
    bool Delete(uint8_t menuId);

    CMenu* GetAt(uint8_t menuId) const;
    bool GetSlotState(uint8_t menuId) const;

    void ShowMenu(uint8_t menuId);
    void HideMenu(uint8_t menuId);
    void Reset();
    void Process();
    void Render(ImGuiRenderer* renderer);
    bool OnTouchEvent(int type, int x, int y);

    char* GetTextPointer(const char* name);
    uint8_t GetCurrentMenuId() const { return m_byteCurrentMenu; }

private:
    void SendMenuSelect(uint8_t row);
    void SendMenuQuit();

private:
    CMenu* m_pMenus[MAX_MENUS];
    bool m_bMenuSlotState[MAX_MENUS];
    uint8_t m_byteCurrentMenu;
    bool m_bExited;
};
