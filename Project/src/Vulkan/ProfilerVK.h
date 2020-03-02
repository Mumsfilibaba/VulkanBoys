#pragma once

#define NOMINMAX

#include "Common/IProfiler.h"
#include "Vulkan/QueryPoolVK.h"

#include <string>
#include <vector>

class CommandBufferVK;
class DeviceVK;

struct Timestamp {
    std::string name;
    uint64_t time;
    // Stores the query index of each beginning timestamp, the query index of the ending timestamp is always the sequent index
    std::vector<uint32_t> queries;
};

class ProfilerVK : public IProfiler
{
public:
    ProfilerVK(const std::string& name, DeviceVK* pDevice);
    ~ProfilerVK();

    void setParentProfiler(ProfilerVK* pParentProfiler);

    // pResetCmdBuffer is used to reset the query pool
    void beginFrame(size_t currentFrame, CommandBufferVK* pProfiledCmdBuffer, CommandBufferVK* pResetCmdBuffer);
    void endFrame();
    // Fetches timestamp data from Vulkan and writes it to timestamp objects
    void writeResults();
    void drawResults();

    void addChildProfiler(ProfilerVK* pChildProfiler);

    void initTimestamp(Timestamp* pTimestamp, const std::string name);
    void beginTimestamp(Timestamp* pTimestamp);
    void endTimestamp(Timestamp* pTimestamp);

    uint32_t getRecurseDepth() const { return m_RecurseDepth; }

private:
    void findWidestText();
    void expandQueryPools();

private:
    // Multiplying factor used to convert a timestamp unit to milliseconds
    static double m_TimestampToMillisec;
    static const uint32_t m_DashesPerRecurse;
    // The widest string written when rendering profiler results, eg: "--Mesh" is 4 characters wide
    static uint32_t m_MaxTextWidth;

private:
    std::vector<ProfilerVK*> m_Children;

    // The amount of levels to root level in the profiler tree
    uint32_t m_RecurseDepth;

    std::vector<Timestamp*> m_Timestamps;

    std::string m_Name;
    uint64_t m_Time;

    QueryPoolVK* m_ppQueryPools[MAX_FRAMES_IN_FLIGHT];
    DeviceVK* m_pDevice;
    CommandBufferVK* m_pProfiledCommandBuffer;

    uint32_t m_CurrentFrame, m_NextQuery;
    std::vector<uint64_t> m_TimeResults;
};
