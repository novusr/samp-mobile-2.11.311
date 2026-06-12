#include "samp/main.h"
#include "samp/Multiplayer/Multiplayer.h"
#include "samp/Network/Network.h"
#include "samp/Network/Pools/TextLabelPool.h"
#include "samp/GUI/imguirenderer.h"
#include "samp/GUI/uisettings.h"
#include "samp/Multiplayer/AimStuff.h"
#include "gta-reversed/game_sa/World.h"
#include "vendor/encoding/encoding.h"

#include <cstring>
#include <string>
#include <vector>

extern CNetGame* pNetGame;
extern CGame* pGame;

namespace
{
    constexpr float kPlayerHeadOffsetZ = 0.23f;
    constexpr float kMinScreenZ = 1.0f;

    using CalcScreenCoorsFn = void (*)(CVector*, CVector*, float*, float*, bool, bool);

    // 2.11 libGame.so.lst:
    // _ZN7CSprite15CalcScreenCoorsERK5RwV3dPS0_PfS4_bb = 0x5F449C
    constexpr uintptr_t kCSpriteCalcScreenCoors_211 = 0x5F449C;

    bool ProjectWorldToScreen(const CVector& worldPos, CVector& screenPos)
    {
        if (!g_libGTASA) return false;

        screenPos = CVector{0.0f, 0.0f, 0.0f};
        reinterpret_cast<CalcScreenCoorsFn>(g_libGTASA + kCSpriteCalcScreenCoors_211)(
            const_cast<CVector*>(&worldPos), &screenPos, nullptr, nullptr, false, false);

        return screenPos.z >= kMinScreenZ;
    }

    std::vector<std::string> SplitLinesWithPCColorCarry(const char* rawText)
    {
        std::vector<std::string> lines;
        if (!rawText || rawText[0] == '\0') return lines;

        std::string source(rawText);
        std::string currentLine;
        std::string activeColor;

        for (size_t i = 0; i < source.size(); ++i)
        {
            if (source[i] == '{' && i + 7 < source.size() && source[i + 7] == '}')
            {
                activeColor.assign(source, i, 8);
                currentLine.append(activeColor);
                i += 7;
                continue;
            }

            if (source[i] == '\n')
            {
                lines.push_back(currentLine);
                currentLine.clear();
                if (!activeColor.empty())
                    currentLine.append(activeColor);
                continue;
            }

            currentLine.push_back(source[i] == '\t' ? ' ' : source[i]);
        }

        lines.push_back(currentLine);
        return lines;
    }
}

C3DTextLabelPool::C3DTextLabelPool()
{
    for (uint16_t i = 0; i < MAX_TEXT_LABELS; ++i)
    {
        m_pTextLabels[i] = nullptr;
        m_bSlotState[i] = false;
    }
}

C3DTextLabelPool::~C3DTextLabelPool()
{
    Reset();
}

bool C3DTextLabelPool::GetSlotState(uint16_t labelId) const
{
    return labelId < MAX_TEXT_LABELS && m_bSlotState[labelId] && m_pTextLabels[labelId] != nullptr;
}

bool C3DTextLabelPool::IsInlineColorCode(const char* text)
{
    if (!text || text[0] != '{') return false;
    for (int i = 1; i <= 7; ++i)
    {
        if (text[i] == '\0') return false;
    }
    if (text[7] != '}') return false;

    for (int i = 1; i < 7; ++i)
    {
        const char c = text[i];
        const bool isHex = (c >= '0' && c <= '9') ||
                           (c >= 'A' && c <= 'F') ||
                           (c >= 'a' && c <= 'f');
        if (!isHex) return false;
    }

    return true;
}

void C3DTextLabelPool::FilterColors(char* text)
{
    if (!text) return;

    char* src = text;
    char* dst = text;

    while (*src != '\0')
    {
        if (IsInlineColorCode(src))
        {
            src += 8;
            continue;
        }

        *dst++ = (*src == '\t') ? ' ' : *src;
        ++src;
    }

    *dst = '\0';
}

void C3DTextLabelPool::CopyTextToLabel(TEXT_LABEL* label, const char* text)
{
    if (!label) return;

    std::string utfText = Encoding::cp2utf(text ? text : "");
    if (utfText.size() > 2048)
        utfText.resize(2048);

    memset(label->text, 0, sizeof(label->text));
    memset(label->textWithoutColors, 0, sizeof(label->textWithoutColors));

    strncpy(label->text, utfText.c_str(), sizeof(label->text) - 1);
    strncpy(label->textWithoutColors, utfText.c_str(), sizeof(label->textWithoutColors) - 1);
    FilterColors(label->textWithoutColors);
}

float C3DTextLabelPool::DistanceSquared(const CVector& a, const CVector& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

void C3DTextLabelPool::CreateTextLabel(uint16_t labelId, const char* text, uint32_t color,
                                       const CVector& pos, float drawDistance, bool useLOS,
                                       uint16_t attachedToPlayerID, uint16_t attachedToVehicleID)
{
    if (labelId >= MAX_TEXT_LABELS) return;

    Delete(labelId);

    TEXT_LABEL* label = new TEXT_LABEL;
    memset(label, 0, sizeof(TEXT_LABEL));

    CopyTextToLabel(label, text);

    label->color = color;
    label->pos = pos;
    label->drawDistance = drawDistance;
    label->useLineOfSight = useLOS;
    label->attachedToPlayerID = attachedToPlayerID;
    label->attachedToVehicleID = attachedToVehicleID;
    label->offsetCoords = (attachedToPlayerID != INVALID_PLAYER_ID || attachedToVehicleID != INVALID_VEHICLE_ID)
                          ? pos : CVector{0.0f, 0.0f, 0.0f};

    m_pTextLabels[labelId] = label;
    m_bSlotState[labelId] = true;
}

void C3DTextLabelPool::Delete(uint16_t labelId)
{
    if (labelId >= MAX_TEXT_LABELS) return;

    if (m_pTextLabels[labelId])
    {
        delete m_pTextLabels[labelId];
        m_pTextLabels[labelId] = nullptr;
    }

    m_bSlotState[labelId] = false;
}

void C3DTextLabelPool::Update3DLabel(uint16_t labelId, uint32_t color, const char* text)
{
    if (!GetSlotState(labelId)) return;

    TEXT_LABEL* label = m_pTextLabels[labelId];
    label->color = color;
    CopyTextToLabel(label, text);
}

void C3DTextLabelPool::Reset()
{
    for (uint16_t i = 0; i < MAX_TEXT_LABELS; ++i)
    {
        Delete(i);
    }
}

bool C3DTextLabelPool::GetLabelWorldPosition(TEXT_LABEL* label, CVector& outPos) const
{
    if (!label || !pNetGame) return false;

    if (label->attachedToPlayerID != INVALID_PLAYER_ID)
    {
        CPlayerPool* playerPool = pNetGame->GetPlayerPool();
        if (!playerPool) return false;

        CPlayerPed* playerPed = nullptr;
        if (label->attachedToPlayerID == playerPool->GetLocalPlayerID())
        {
            CLocalPlayer* localPlayer = playerPool->GetLocalPlayer();
            if (localPlayer) playerPed = localPlayer->GetPlayerPed();
        }
        else
        {
            CRemotePlayer* remotePlayer = playerPool->GetAt(label->attachedToPlayerID);
            if (remotePlayer && remotePlayer->IsActive())
                playerPed = remotePlayer->GetPlayerPed();
        }

        if (!playerPed || !playerPed->m_pPed || !playerPed->m_pPed->IsAdded()) return false;

        playerPed->GetBonePosition(8, &outPos);
        outPos += label->offsetCoords;
        outPos.z += kPlayerHeadOffsetZ;
        return true;
    }

    if (label->attachedToVehicleID != INVALID_VEHICLE_ID)
    {
        CVehiclePool* vehiclePool = pNetGame->GetVehiclePool();
        if (!vehiclePool || !vehiclePool->GetSlotState(label->attachedToVehicleID)) return false;

        CVehicle* vehicle = vehiclePool->GetAt(label->attachedToVehicleID);
        if (!vehicle || !vehicle->m_pVehicle || !vehicle->m_pVehicle->IsAdded()) return false;

        outPos = vehicle->m_pVehicle->GetPosition();
        outPos += label->offsetCoords;
        return true;
    }

    outPos = label->pos;
    return true;
}

void C3DTextLabelPool::DrawLabel(ImGuiRenderer* renderer, TEXT_LABEL* label, const CVector& worldPos)
{
    if (!renderer || !label || !pGame || !pNetGame) return;

    CPlayerPool* playerPool = pNetGame->GetPlayerPool();
    if (!playerPool || !playerPool->GetLocalPlayer() || !playerPool->GetLocalPlayer()->GetPlayerPed()) return;

    CPlayerPed* localPed = playerPool->GetLocalPlayer()->GetPlayerPed();
    if (!localPed || !localPed->m_pPed || !localPed->m_pPed->IsAdded()) return;

    const CVector localPos = localPed->m_pPed->GetPosition();
    if (label->drawDistance > 0.0f &&
        DistanceSquared(localPos, worldPos) > label->drawDistance * label->drawDistance)
    {
        return;
    }

    if (label->useLineOfSight)
    {
        CAMERA_AIM* cam = GameGetInternalAim();
        if (!cam) return;

        const CVector cameraPos{cam->pos1x, cam->pos1y, cam->pos1z};
        if (!CWorld::GetIsLineOfSightClear(cameraPos, worldPos, true, false, false, true, false, false, false))
            return;
    }

    CVector screenPos;
    if (!ProjectWorldToScreen(worldPos, screenPos)) return;

    const float fontSize = UISettings::fontSize() / 2.0f;
    const std::vector<std::string> lines = SplitLinesWithPCColorCarry(label->text);
    if (lines.empty()) return;

    const float totalHeight = fontSize * static_cast<float>(lines.size());
    float y = screenPos.y - (totalHeight * 0.5f);

    for (const std::string& line : lines)
    {
        if (line.empty())
        {
            y += fontSize;
            continue;
        }

        const float width = renderer->calculateTextSize(line, fontSize).x;
        ImVec2 drawPos(screenPos.x - (width * 0.5f), y);
        renderer->drawText(drawPos, __builtin_bswap32(label->color), line, true, fontSize);
        y += fontSize;
    }
}

void C3DTextLabelPool::Render(ImGuiRenderer* renderer)
{
    if (!renderer || !pNetGame) return;

    for (uint16_t i = 0; i < MAX_TEXT_LABELS; ++i)
    {
        if (!GetSlotState(i)) continue;

        TEXT_LABEL* label = m_pTextLabels[i];
        if (!label || label->text[0] == '\0') continue;

        CVector worldPos;
        if (!GetLabelWorldPosition(label, worldPos)) continue;

        DrawLabel(renderer, label, worldPos);
    }
}
