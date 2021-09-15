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

#ifndef SENSOR_PROXY_H
#define SENSOR_PROXY_H

#include <map>
#include "sensor_agent_type.h"
#include "refbase.h"
#include "sensor_data_channel.h"

struct SensorNativeData;
struct SensorIdList;
typedef int32_t (*SensorDataCallback)(struct SensorNativeData *events, uint32_t num);

struct SensorAgentProxy : public OHOS::RefBase {
public:
    SensorAgentProxy();
    ~SensorAgentProxy();
    static const SensorAgentProxy *GetSensorsObj();
    int32_t CreateSensorDataChannel(const SensorUser *user) const;
    int32_t DestroySensorDataChannel() const;
    int32_t GetSensorId(struct SensorIdList *sensorId, uint32_t sensorGroup, uint32_t sensorType) const;
    int32_t GetDefaultSensorId(uint32_t sensorGroup, uint32_t sensorType) const;
    int64_t GetSensorMinSamplePeriod(uint32_t sensorId) const;
    int32_t ChanageSensorInterval(uint32_t sensorId, int64_t sampingPeriodNs, int64_t maxReportDelay) const;
    int32_t ActivateSensor(int32_t sensorId, const SensorUser *user) const;
    int32_t DeactivateSensor(int32_t sensorId, const SensorUser *user) const;
    int32_t SetBatch(int32_t sensorId, const SensorUser *user, int64_t samplingInterval, int64_t reportInterval) const;
    int32_t SubscribeSensor(int32_t sensorId, const SensorUser *user) const;
    int32_t UnsubscribeSensor(int32_t sensorId, const SensorUser *user) const;
    int32_t SetMode(int32_t sensorId, const SensorUser *user, int32_t mode) const;
    int32_t SetOption(int32_t sensorId, const SensorUser *user, int32_t option) const;
    int32_t GetAllSensors(SensorInfo **sensorInfo, int32_t *count) const;

private:
    static void HandleSensorData(SensorEvent *events, int32_t num, void *data);
    static void FillSensorAccuracy(struct SensorNativeData &data, SensorEvent &event);
    static OHOS::sptr<SensorAgentProxy> sensorObj_;
    OHOS::sptr<OHOS::Sensors::SensorDataChannel> dataChannel_;
    static SensorUser *user_;
    static const std::map<int32_t, int32_t> g_sensorDataLenthMap;
};
#endif  // endif SENSOR_PROXY_H