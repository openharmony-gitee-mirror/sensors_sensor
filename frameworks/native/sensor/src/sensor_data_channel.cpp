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

#include "sensor_data_channel.h"

#include <cerrno>
#include <unistd.h>

#include <fcntl.h>
#include <sys/socket.h>

#include "my_file_descriptor_listener.h"
#include "sensors_errors.h"
#include "sensors_log_domain.h"
#include "string_ex.h"

#ifndef O_NONBLOCK
# define O_NONBLOCK	  04000
#endif

namespace OHOS {
namespace Sensors {
using namespace OHOS::HiviewDFX;
using namespace OHOS::AppExecFwk;
std::shared_ptr<MyEventHandler> SensorDataChannel::eventHandler_;
std::shared_ptr<AppExecFwk::EventRunner> SensorDataChannel::eventRunner_;
int32_t SensorDataChannel::receiveFd_ = 0;

namespace {
constexpr HiLogLabel LABEL = { LOG_CORE, SensorsLogDomain::SENSOR_NATIVE, "SensorDataChannel" };
// max 100 data in cache buffer
constexpr int32_t SENSOR_READ_DATA_SIZE = sizeof(struct SensorEvent) * 100;
const uint32_t STOP_EVENT_ID = 0;
}  // namespace

SensorDataChannel::SensorDataChannel()
    : dataCB_(nullptr),
      privateData_(nullptr),
      threadStop_(true)
{
    receiveDataBuff_ = new (std::nothrow) SensorEvent[SENSOR_READ_DATA_SIZE];
    if (receiveDataBuff_ == nullptr) {
        HiLog::Error(LABEL, "%{public}s receiveDataBuff_ cannot be null", __func__);
        return;
    }
}

int32_t SensorDataChannel::HandleEvent(int32_t fd, int32_t events, void *data)
{
    HiLog::Debug(LABEL, "%{public}s fd : %{public}d", __func__, fd);
    constexpr int32_t allowSkip = 1;
    if (data == nullptr) {
        HiLog::Error(LABEL, "%{public}s data cannot be null", __func__);
        return allowSkip;
    }
    SensorDataChannel *channel = reinterpret_cast<SensorDataChannel *>(data);
    if (channel->receiveDataBuff_ == nullptr) {
        HiLog::Error(LABEL, "%{public}s channel receiveDataBuff_ cannot be null", __func__);
        return allowSkip;
    }
    int32_t len = channel->ReceiveData(channel->receiveDataBuff_, SENSOR_READ_DATA_SIZE);
    while (len > 0) {
        int32_t eventSize = sizeof(SensorEvent);
        if (eventSize > 0) {
            int32_t num = len / eventSize;
            HiLog::Debug(LABEL, "%{public}s num : %{public}d", __func__, num);
            channel->dataCB_(channel->receiveDataBuff_, num, channel->privateData_);
            len = channel->ReceiveData(channel->receiveDataBuff_, SENSOR_READ_DATA_SIZE);
        }
    }
    return allowSkip;
}

int32_t SensorDataChannel::CreateSensorDataChannel(DataChannelCB callBack, void *data)
{
    if (callBack == nullptr) {
        HiLog::Error(LABEL, "%{public}s callBack cannot be null", __func__);
        return SENSOR_NATIVE_REGSITER_CB_ERR;
    }
    dataCB_ = callBack;
    privateData_ = data;
    return InnerSensorDataChannel();
}

int32_t SensorDataChannel::RestoreSensorDataChannel()
{
    if (dataCB_ == nullptr) {
        HiLog::Error(LABEL, "%{public}s dataCB_ cannot be null", __func__);
        return SENSOR_CHANNEL_RESTORE_CB_ERR;
    }
    if (GetReceiveDataFd() != INVALID_FD) {
        HiLog::Error(LABEL, "%{public}s fd not close", __func__);
        return SENSOR_CHANNEL_RESTORE_FD_ERR;
    }
    if (sensorDataThread_.joinable()) {
        HiLog::Error(LABEL, "%{public}s  thread exit", __func__);
        return SENSOR_CHANNEL_RESTORE_THREAD_ERR;
    }
    return InnerSensorDataChannel();
}

int32_t SensorDataChannel::InnerSensorDataChannel()
{
    std::lock_guard<std::mutex> threadLock(treadMutex_);

    // create basic data channel
    int32_t ret = CreateSensorBasicChannel(SENSOR_READ_DATA_SIZE, SENSOR_READ_DATA_SIZE);
    if (ret != ERR_OK) {
        HiLog::Error(LABEL, "%{public}s create basic channel failed, ret : %{public}d", __func__, ret);
        return ret;
    }
    auto listener = std::make_shared<MyFileDescriptorListener>();
    listener->SetChannel(this);
    auto myRunner = AppExecFwk::EventRunner::Create(true);
    if (myRunner == nullptr) {
        HiLog::Error(LABEL, "%{public}s myRunner is null", __func__);
        return -1;
    }
    auto handler = std::make_shared<MyEventHandler>(myRunner);
    if (handler == nullptr) {
        HiLog::Error(LABEL, "%{public}s handler is null", __func__);
        return -1;
    }

    receiveFd_ = GetReceiveDataFd();
    auto inResult = handler->AddFileDescriptorListener(receiveFd_, AppExecFwk::FILE_DESCRIPTOR_INPUT_EVENT, listener);
    if (inResult != 0) {
        HiLog::Error(LABEL, "%{public}s AddFileDescriptorListener fail", __func__);
        return -1;
    }
    eventHandler_ = handler;
    eventRunner_ = myRunner;
    int64_t delayTime = 100;
    int64_t param = 0;
    bool sendEventResult = eventHandler_->SendEvent(STOP_EVENT_ID, param, delayTime);
    if (!sendEventResult) {
        HiLog::Error(LABEL, "%{public}s EventHandler SendEvent fail", __func__);
        return -1;
    }
    int32_t runResult = eventRunner_->Run();
    if (!runResult) {
        HiLog::Error(LABEL, "%{public}s EventRunner run fail", __func__);
        return -1;
    }
    return ERR_OK;
}

int32_t SensorDataChannel::DestroySensorDataChannel()
{
    std::lock_guard<std::mutex> threadLock(treadMutex_);
    // send wakeup signal to sensor_fwk_read_data thread

    if (eventHandler_ == nullptr) {
        HiLog::Error(LABEL, "%{public}s handler is null", __func__);
        return -1;
    }
    int32_t fd = GetReceiveDataFd();
    eventHandler_->RemoveFileDescriptorListener(fd);

    threadStop_ = true;

    if (sensorDataThread_.joinable()) {
        sensorDataThread_.join();
    }
    // destroy sensor basic channel
    return DestroySensorBasicChannel();
}
bool SensorDataChannel::IsThreadExit()
{
    return (!sensorDataThread_.joinable()) && (threadStop_);
}

bool SensorDataChannel::IsThreadStart()
{
    return (sensorDataThread_.joinable()) && (!threadStop_);
}

SensorDataChannel::~SensorDataChannel()
{
    DestroySensorDataChannel();
    if (receiveDataBuff_ != nullptr) {
        delete[] receiveDataBuff_;
    }
}
}  // namespace Sensors
}  // namespace OHOS