/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sensor_manager.h"

#include "sensor.h"
#include "sensors_errors.h"
#include "sensors_log_domain.h"

namespace OHOS {
namespace Sensors {
using namespace OHOS::HiviewDFX;

namespace {
constexpr HiLogLabel LABEL = { LOG_CORE, SensorsLogDomain::SENSOR_SERVICE, "SensorManager" };
constexpr uint32_t INVALID_SENSOR_ID = -1;
constexpr uint32_t PROXIMITY_SENSOR_ID = 50331904;
constexpr float PROXIMITY_FAR = 5.0;
}  // namespace

void SensorManager::InitSensorMap(std::unordered_map<uint32_t, Sensor> &sensorMap,
                                  sptr<SensorDataProcesser> dataProcesser, sptr<ReportDataCallback> dataCallback)
{
    std::lock_guard<std::mutex> sensorLock(sensorMapMutex_);
    sensorMap_.insert(sensorMap.begin(), sensorMap.end());
    sensorDataProcesser_ = dataProcesser;
    reportDataCallback_ = dataCallback;
    HiLog::Debug(LABEL, "%{public}s begin sensorMap_.size : %{public}d", __func__, int32_t { sensorMap_.size() });
    return;
}

uint32_t SensorManager::GetSensorFlag(uint32_t sensorId)
{
    uint32_t flag = SENSOR_ONE_SHOT;
    auto sensor = sensorMap_.find(sensorId);
    if (sensor != sensorMap_.end()) {
        flag = sensor->second.GetFlags();
    }
    return flag;
}

bool SensorManager::SetBestSensorParams(uint32_t sensorId, int64_t samplingPeriodNs, int64_t maxReportDelayNs)
{
    HiLog::Debug(LABEL, "%{public}s begin, sensorId : %{public}u, samplingPeriodNs : %{public}d", __func__, sensorId,
                 int32_t { samplingPeriodNs });
    if (sensorId == INVALID_SENSOR_ID) {
        HiLog::Error(LABEL, "%{public}s sensorId is invalid", __func__);
        return false;
    }
    SensorBasicInfo sensorInfo = clientInfo_.GetBestSensorInfo(sensorId);
    int64_t bestSamplingPeriodNs = sensorInfo.GetSamplingPeriodNs();
    int64_t bestReportDelayNs = sensorInfo.GetMaxReportDelayNs();
    if ((samplingPeriodNs > bestSamplingPeriodNs) && (maxReportDelayNs > bestReportDelayNs)) {
        HiLog::Debug(LABEL, "%{public}s no need to reset sensor params", __func__);
        return true;
    }
    bestSamplingPeriodNs = (samplingPeriodNs < bestSamplingPeriodNs) ? samplingPeriodNs : bestSamplingPeriodNs;
    bestReportDelayNs = (maxReportDelayNs < bestReportDelayNs) ? maxReportDelayNs : bestReportDelayNs;
    HiLog::Debug(LABEL, "%{public}s bestSamplingPeriodNs : %{public}d", __func__, int32_t { bestSamplingPeriodNs });
    auto ret = sensorServiceImpl_.SetSensorConfig(sensorId, bestSamplingPeriodNs, bestReportDelayNs);
    if (ret != ERR_OK) {
        HiLog::Error(LABEL, "%{public}s SetSensorConfig failed", __func__);
        return false;
    }
    HiLog::Debug(LABEL, "%{public}s end", __func__);
    return true;
}

bool SensorManager::ResetBestSensorParams(uint32_t sensorId)
{
    HiLog::Debug(LABEL, "%{public}s begin, sensorId : %{public}u", __func__, sensorId);
    if (sensorId == INVALID_SENSOR_ID) {
        HiLog::Error(LABEL, "%{public}s sensorId is invalid", __func__);
        return false;
    }
    SensorBasicInfo sensorInfo = clientInfo_.GetBestSensorInfo(sensorId);
    auto ret = sensorServiceImpl_.SetSensorConfig(sensorId, sensorInfo.GetSamplingPeriodNs(), 
                                                  sensorInfo.GetMaxReportDelayNs());
    if (ret != ERR_OK) {
        HiLog::Error(LABEL, "%{public}s SetSensorConfig failed", __func__);
        return false;
    }
    HiLog::Debug(LABEL, "%{public}s end", __func__);
    return true;
}

SensorBasicInfo SensorManager::GetSensorInfo(uint32_t sensorId, int64_t samplingPeriodNs, int64_t maxReportDelayNs)
{
    HiLog::Debug(LABEL, "%{public}s begin, sensorId : %{public}u", __func__, sensorId);
    SensorBasicInfo sensorInfo;
    std::lock_guard<std::mutex> sensorMapLock(sensorMapMutex_);
    auto it = sensorMap_.find(sensorId);
    if (it == sensorMap_.end()) {
        sensorInfo.SetSamplingPeriodNs(samplingPeriodNs);
        sensorInfo.SetMaxReportDelayNs(maxReportDelayNs);
        sensorInfo.SetSensorState(SENSOR_ENABLED);
        HiLog::Error(LABEL, "%{public}s sensorId invalid", __func__);
        return sensorInfo;
    }
    int64_t curSamplingPeriodNs =
        (samplingPeriodNs < it->second.GetMinSamplePeriodNs()) ? it->second.GetMinSamplePeriodNs() : samplingPeriodNs;
    if (it->second.GetMaxSamplePeriodNs() != 0) {
        curSamplingPeriodNs = (samplingPeriodNs > it->second.GetMaxSamplePeriodNs()) ? it->second.GetMaxSamplePeriodNs()
                                                                                     : curSamplingPeriodNs;
    }
    int32_t maxEventCount = it->second.GetFifoMaxEventCount();
    if ((samplingPeriodNs == 0) || (maxEventCount > (INT64_MAX / samplingPeriodNs))) {
        HiLog::Error(LABEL, "%{public}s failed, samplingPeriodNs overflow", __func__);
        return sensorInfo;
    }
    int64_t supportDelay = samplingPeriodNs * maxEventCount;
    int64_t curReportDelayNs = (maxReportDelayNs > supportDelay) ? supportDelay : maxReportDelayNs;
    sensorInfo.SetSamplingPeriodNs(curSamplingPeriodNs);
    sensorInfo.SetMaxReportDelayNs(curReportDelayNs);
    sensorInfo.SetSensorState(SENSOR_ENABLED);
    HiLog::Debug(LABEL, "%{public}s end", __func__);
    return sensorInfo;
}

ErrCode SensorManager::SaveSubscriber(uint32_t sensorId, uint32_t pid, int64_t samplingPeriodNs,
    int64_t maxReportDelayNs)
{
    SensorBasicInfo sensorInfo = GetSensorInfo(sensorId, samplingPeriodNs, maxReportDelayNs);
    auto updateRet = clientInfo_.UpdateSensorInfo(sensorId, pid, sensorInfo);
    if (!updateRet) {
        HiLog::Error(LABEL, "%{public}s UpdateSensorInfo failed", __func__);
        return UPDATE_SENSOR_INFO_ERR;
    }
    return ERR_OK;
}

void SensorManager::StartDataReportThread()
{
    HiLog::Debug(LABEL, "%{public}s begin", __func__);
    if (!dataThread_.joinable()) {
        HiLog::Warn(LABEL, "%{public}s dataThread_ started", __func__);
        std::thread senocdDataThread(SensorDataProcesser::DataThread, sensorDataProcesser_, reportDataCallback_);
        dataThread_ = std::move(senocdDataThread);
    }
    HiLog::Debug(LABEL, "%{public}s end", __func__);
}

bool SensorManager::IsOtherClientUsingSensor(uint32_t sensorId, int32_t clientPid)
{
    HiLog::Debug(LABEL, "%{public}s begin, sensorId : %{public}u", __func__, sensorId);
    if (clientInfo_.OnlyCurPidSensorEnabled(sensorId, clientPid)) {
        HiLog::Warn(LABEL, "%{public}s Only current client using this sensor", __func__);
        return false;
    }
    clientInfo_.ClearCurPidSensorInfo(sensorId, clientPid);
    if (!ResetBestSensorParams(sensorId)) {
        HiLog::Warn(LABEL, "%{public}s ResetBestSensorParams failed", __func__);
    }
    HiLog::Debug(LABEL, "%{public}s other client is using this sensor", __func__);
    return true;
}

ErrCode SensorManager::AfterDisableSensor(uint32_t sensorId)
{
    HiLog::Debug(LABEL, "%{public}s begin", __func__);
    if (!clientInfo_.ClearSensorInfo(sensorId)) {
        HiLog::Error(LABEL, "%{public}s ClearSensorInfo failed", __func__);
        return CLEAR_SENSOR_INFO_ERR;
    }
    if (sensorId == PROXIMITY_SENSOR_ID) {
        struct SensorEvent event;
        auto ret = clientInfo_.GetStoreEvent(sensorId, event);
        if (ret == ERR_OK) {
            HiLog::Debug(LABEL, "%{public}s change the default state is far", __func__);
            // event.light.data[0] = PROXIMITY_FAR;
            event.data[0] = PROXIMITY_FAR;
            clientInfo_.StoreEvent(event);
        }
    }
    HiLog::Debug(LABEL, "%{public}s end", __func__);
    return ERR_OK;
}

void SensorManager::GetPackageNameFromUid(int32_t uid, std::string &packageName)
{
    HiLog::Debug(LABEL, "%{public}s begin", __func__);
}
}  // namespace Sensors
}  // namespace OHOS
