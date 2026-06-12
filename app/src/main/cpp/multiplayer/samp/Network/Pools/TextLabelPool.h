#pragma once

#include <cstdint>

#include "gta-reversed/game_sa/Core/Vector.h"

class ImGuiRenderer;

static constexpr uint16_t INVALID_3D_TEXT_LABEL = 0xFFFF;

#pragma pack(push, 1)
struct TEXT_LABEL
{
    char text[2048 + 1];
    char textWithoutColors[2048 + 1];
    uint32_t color;
    CVector pos;
    float drawDistance;
    bool useLineOfSight;
    uint16_t attachedToPlayerID;
    uint16_t attachedToVehicleID;
    CVector offsetCoords;
};
#pragma pack(pop)

class C3DTextLabelPool
{
public:
    C3DTextLabelPool();
    ~C3DTextLabelPool();

    bool GetSlotState(uint16_t labelId) const;

    void CreateTextLabel(uint16_t labelId, const char* text, uint32_t color,
                         const CVector& pos, float drawDistance, bool useLOS,
                         uint16_t attachedToPlayerID, uint16_t attachedToVehicleID);
    void Delete(uint16_t labelId);
    void Update3DLabel(uint16_t labelId, uint32_t color, const char* text);
    void Reset();
    void Render(ImGuiRenderer* renderer);

private:
    bool GetLabelWorldPosition(TEXT_LABEL* label, CVector& outPos) const;
    void DrawLabel(ImGuiRenderer* renderer, TEXT_LABEL* label, const CVector& worldPos);

    static void FilterColors(char* text);
    static bool IsInlineColorCode(const char* text);
    static void CopyTextToLabel(TEXT_LABEL* label, const char* text);
    static float DistanceSquared(const CVector& a, const CVector& b);

private:
    TEXT_LABEL* m_pTextLabels[MAX_TEXT_LABELS];
    bool m_bSlotState[MAX_TEXT_LABELS];
};
