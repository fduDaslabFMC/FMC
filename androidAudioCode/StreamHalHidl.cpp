/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "StreamHalHidl"
//#define LOG_NDEBUG 0

#include <android/hardware/audio/2.0/IStreamOutCallback.h>
#include <hwbinder/IPCThreadState.h>
#include <mediautils/SchedulingPolicyService.h>
#include <utils/Log.h>

#include "DeviceHalHidl.h"
#include "EffectHalHidl.h"
#include "StreamHalHidl.h"
#include <cstring>
#include <math.h>

#define M_PI 3.14159265358979323846
#define myTAG "StreamHalHidl_Chen"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, myTAG,__VA_ARGS__)

using ::android::hardware::audio::common::V2_0::AudioChannelMask;
using ::android::hardware::audio::common::V2_0::AudioFormat;
using ::android::hardware::audio::common::V2_0::ThreadInfo;
using ::android::hardware::audio::V2_0::AudioDrain;
using ::android::hardware::audio::V2_0::IStreamOutCallback;
using ::android::hardware::audio::V2_0::MessageQueueFlagBits;
using ::android::hardware::audio::V2_0::MmapBufferInfo;
using ::android::hardware::audio::V2_0::MmapPosition;
using ::android::hardware::audio::V2_0::ParameterValue;
using ::android::hardware::audio::V2_0::Result;
using ::android::hardware::audio::V2_0::TimeSpec;
using ::android::hardware::MQDescriptorSync;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ReadCommand = ::android::hardware::audio::V2_0::IStreamIn::ReadCommand;

namespace android {

PitchShift* pitchShifter = NULL;
Butterworth* lowPassFilter = NULL;
MyConverter* dataConverter = NULL;
bool voicePrintControl = false;
bool highFreqControl = false;
bool sodControl = false;

StreamHalHidl::StreamHalHidl(IStream *stream)
        : ConversionHelperHidl("Stream"),
          mStream(stream),
          mHalThreadPriority(HAL_THREAD_PRIORITY_DEFAULT) {
}

StreamHalHidl::~StreamHalHidl() {
    mStream = nullptr;
}

status_t StreamHalHidl::getSampleRate(uint32_t *rate) {
    if (!mStream) return NO_INIT;
    return processReturn("getSampleRate", mStream->getSampleRate(), rate);
}

status_t StreamHalHidl::getBufferSize(size_t *size) {
    if (!mStream) return NO_INIT;
    return processReturn("getBufferSize", mStream->getBufferSize(), size);
}

status_t StreamHalHidl::getChannelMask(audio_channel_mask_t *mask) {
    if (!mStream) return NO_INIT;
    return processReturn("getChannelMask", mStream->getChannelMask(), mask);
}

status_t StreamHalHidl::getFormat(audio_format_t *format) {
    if (!mStream) return NO_INIT;
    return processReturn("getFormat", mStream->getFormat(), format);
}

status_t StreamHalHidl::getAudioProperties(
        uint32_t *sampleRate, audio_channel_mask_t *mask, audio_format_t *format) {
    if (!mStream) return NO_INIT;
    Return<void> ret = mStream->getAudioProperties(
            [&](uint32_t sr, AudioChannelMask m, AudioFormat f) {
                *sampleRate = sr;
                *mask = static_cast<audio_channel_mask_t>(m);
                *format = static_cast<audio_format_t>(f);
            });
    return processReturn("getAudioProperties", ret);
}

status_t StreamHalHidl::setParameters(const String8& kvPairs) {
    if (!mStream) return NO_INIT;
    hidl_vec<ParameterValue> hidlParams;
    status_t status = parametersFromHal(kvPairs, &hidlParams);
    if (status != OK) return status;
    return processReturn("setParameters", mStream->setParameters(hidlParams));
}

status_t StreamHalHidl::getParameters(const String8& keys, String8 *values) {
    values->clear();
    if (!mStream) return NO_INIT;
    hidl_vec<hidl_string> hidlKeys;
    status_t status = keysFromHal(keys, &hidlKeys);
    if (status != OK) return status;
    Result retval;
    Return<void> ret = mStream->getParameters(
            hidlKeys,
            [&](Result r, const hidl_vec<ParameterValue>& parameters) {
                retval = r;
                if (retval == Result::OK) {
                    parametersToHal(parameters, values);
                }
            });
    return processReturn("getParameters", ret, retval);
}

status_t StreamHalHidl::addEffect(sp<EffectHalInterface> effect) {
    if (!mStream) return NO_INIT;
    return processReturn("addEffect", mStream->addEffect(
                    static_cast<EffectHalHidl*>(effect.get())->effectId()));
}

status_t StreamHalHidl::removeEffect(sp<EffectHalInterface> effect) {
    if (!mStream) return NO_INIT;
    return processReturn("removeEffect", mStream->removeEffect(
                    static_cast<EffectHalHidl*>(effect.get())->effectId()));
}

status_t StreamHalHidl::standby() {
    if (!mStream) return NO_INIT;
    return processReturn("standby", mStream->standby());
}

status_t StreamHalHidl::dump(int fd) {
    if (!mStream) return NO_INIT;
    native_handle_t* hidlHandle = native_handle_create(1, 0);
    hidlHandle->data[0] = fd;
    Return<void> ret = mStream->debugDump(hidlHandle);
    native_handle_delete(hidlHandle);
    return processReturn("dump", ret);
}

status_t StreamHalHidl::start() {
    if (!mStream) return NO_INIT;
    return processReturn("start", mStream->start());
}

status_t StreamHalHidl::stop() {
    if (!mStream) return NO_INIT;
    return processReturn("stop", mStream->stop());
}

status_t StreamHalHidl::createMmapBuffer(int32_t minSizeFrames,
                                  struct audio_mmap_buffer_info *info) {
    Result retval;
    Return<void> ret = mStream->createMmapBuffer(
            minSizeFrames,
            [&](Result r, const MmapBufferInfo& hidlInfo) {
                retval = r;
                if (retval == Result::OK) {
                    const native_handle *handle = hidlInfo.sharedMemory.handle();
                    if (handle->numFds > 0) {
                        info->shared_memory_fd = handle->data[0];
                        info->buffer_size_frames = hidlInfo.bufferSizeFrames;
                        info->burst_size_frames = hidlInfo.burstSizeFrames;
                        // info->shared_memory_address is not needed in HIDL context
                        info->shared_memory_address = NULL;
                    } else {
                        retval = Result::NOT_INITIALIZED;
                    }
                }
            });
    return processReturn("createMmapBuffer", ret, retval);
}

status_t StreamHalHidl::getMmapPosition(struct audio_mmap_position *position) {
    Result retval;
    Return<void> ret = mStream->getMmapPosition(
            [&](Result r, const MmapPosition& hidlPosition) {
                retval = r;
                if (retval == Result::OK) {
                    position->time_nanoseconds = hidlPosition.timeNanoseconds;
                    position->position_frames = hidlPosition.positionFrames;
                }
            });
    return processReturn("getMmapPosition", ret, retval);
}

status_t StreamHalHidl::setHalThreadPriority(int priority) {
    mHalThreadPriority = priority;
    return OK;
}

bool StreamHalHidl::requestHalThreadPriority(pid_t threadPid, pid_t threadId) {
    if (mHalThreadPriority == HAL_THREAD_PRIORITY_DEFAULT) {
        return true;
    }
    int err = requestPriority(
            threadPid, threadId,
            mHalThreadPriority, false /*isForApp*/, true /*asynchronous*/);
    ALOGE_IF(err, "failed to set priority %d for pid %d tid %d; error %d",
            mHalThreadPriority, threadPid, threadId, err);
    // Audio will still work, but latency will be higher and sometimes unacceptable.
    return err == 0;
}

namespace {

/* Notes on callback ownership.

This is how (Hw)Binder ownership model looks like. The server implementation
is owned by Binder framework (via sp<>). Proxies are owned by clients.
When the last proxy disappears, Binder framework releases the server impl.

Thus, it is not needed to keep any references to StreamOutCallback (this is
the server impl) -- it will live as long as HAL server holds a strong ref to
IStreamOutCallback proxy. We clear that reference by calling 'clearCallback'
from the destructor of StreamOutHalHidl.

The callback only keeps a weak reference to the stream. The stream is owned
by AudioFlinger.

*/

struct StreamOutCallback : public IStreamOutCallback {
    StreamOutCallback(const wp<StreamOutHalHidl>& stream) : mStream(stream) {}

    // IStreamOutCallback implementation
    Return<void> onWriteReady()  override {
        sp<StreamOutHalHidl> stream = mStream.promote();
        if (stream != 0) {
            stream->onWriteReady();
        }
        return Void();
    }

    Return<void> onDrainReady()  override {
        sp<StreamOutHalHidl> stream = mStream.promote();
        if (stream != 0) {
            stream->onDrainReady();
        }
        return Void();
    }

    Return<void> onError()  override {
        sp<StreamOutHalHidl> stream = mStream.promote();
        if (stream != 0) {
            stream->onError();
        }
        return Void();
    }

  private:
    wp<StreamOutHalHidl> mStream;
};

}  // namespace

StreamOutHalHidl::StreamOutHalHidl(const sp<IStreamOut>& stream)
        : StreamHalHidl(stream.get()), mStream(stream), mWriterClient(0), mEfGroup(nullptr) {
}

StreamOutHalHidl::~StreamOutHalHidl() {
    if (mStream != 0) {
        if (mCallback.unsafe_get()) {
            processReturn("clearCallback", mStream->clearCallback());
        }
        processReturn("close", mStream->close());
        mStream.clear();
    }
    mCallback.clear();
    hardware::IPCThreadState::self()->flushCommands();
    if (mEfGroup) {
        EventFlag::deleteEventFlag(&mEfGroup);
    }
}

status_t StreamOutHalHidl::getFrameSize(size_t *size) {
    if (mStream == 0) return NO_INIT;
    return processReturn("getFrameSize", mStream->getFrameSize(), size);
}

status_t StreamOutHalHidl::getLatency(uint32_t *latency) {
    if (mStream == 0) return NO_INIT;
    if (mWriterClient == gettid() && mCommandMQ) {
        return callWriterThread(
                WriteCommand::GET_LATENCY, "getLatency", nullptr, 0,
                [&](const WriteStatus& writeStatus) {
                    *latency = writeStatus.reply.latencyMs;
                });
    } else {
        return processReturn("getLatency", mStream->getLatency(), latency);
    }
}

status_t StreamOutHalHidl::setVolume(float left, float right) {
    if (mStream == 0) return NO_INIT;
    return processReturn("setVolume", mStream->setVolume(left, right));
}

status_t StreamOutHalHidl::write(const void *buffer, size_t bytes, size_t *written) {
    if (mStream == 0) return NO_INIT;
    *written = 0;

    if (bytes == 0 && !mDataMQ) {
        // Can't determine the size for the MQ buffer. Wait for a non-empty write request.
        ALOGW_IF(mCallback.unsafe_get(), "First call to async write with 0 bytes");
        return OK;
    }

    status_t status;
    if (!mDataMQ && (status = prepareForWriting(bytes)) != OK) {
        return status;
    }

    //若需要进行并发控制，则将输出流置为0
    if(sodControl){
        void* modifier=const_cast<void*>(buffer);
        memset(modifier,0,bytes);
        sodControl = false;
        LOGD("StreamOutHalHidl--------将%d bytes置为0",(int)bytes);
    }

    status = callWriterThread(
            WriteCommand::WRITE, "write", static_cast<const uint8_t*>(buffer), bytes,
            [&] (const WriteStatus& writeStatus) {
                *written = writeStatus.reply.written;
                // Diagnostics of the cause of b/35813113.
                ALOGE_IF(*written > bytes,
                        "hal reports more bytes written than asked for: %lld > %lld",
                        (long long)*written, (long long)bytes);
            });

    return status;
}

status_t StreamOutHalHidl::callWriterThread(
        WriteCommand cmd, const char* cmdName,
        const uint8_t* data, size_t dataSize, StreamOutHalHidl::WriterCallback callback) {
    if (!mCommandMQ->write(&cmd)) {
        ALOGE("command message queue write failed for \"%s\"", cmdName);
        return -EAGAIN;
    }
    if (data != nullptr) {
        size_t availableToWrite = mDataMQ->availableToWrite();
        if (dataSize > availableToWrite) {
            ALOGW("truncating write data from %lld to %lld due to insufficient data queue space",
                    (long long)dataSize, (long long)availableToWrite);
            dataSize = availableToWrite;
        }
        if (!mDataMQ->write(data, dataSize)) {
            ALOGE("data message queue write failed for \"%s\"", cmdName);
        }
    }
    mEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY));

    // TODO: Remove manual event flag handling once blocking MQ is implemented. b/33815422
    uint32_t efState = 0;
retry:
    status_t ret = mEfGroup->wait(static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL), &efState);
    if (efState & static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL)) {
        WriteStatus writeStatus;
        writeStatus.retval = Result::NOT_INITIALIZED;
        if (!mStatusMQ->read(&writeStatus)) {
            ALOGE("status message read failed for \"%s\"", cmdName);
        }
        if (writeStatus.retval == Result::OK) {
            ret = OK;
            callback(writeStatus);
        } else {
            ret = processReturn(cmdName, writeStatus.retval);
        }
        return ret;
    }
    if (ret == -EAGAIN || ret == -EINTR) {
        // Spurious wakeup. This normally retries no more than once.
        goto retry;
    }
    return ret;
}

status_t StreamOutHalHidl::prepareForWriting(size_t bufferSize) {
    std::unique_ptr<CommandMQ> tempCommandMQ;
    std::unique_ptr<DataMQ> tempDataMQ;
    std::unique_ptr<StatusMQ> tempStatusMQ;
    Result retval;
    pid_t halThreadPid, halThreadTid;
    Return<void> ret = mStream->prepareForWriting(
            1, bufferSize,
            [&](Result r,
                    const CommandMQ::Descriptor& commandMQ,
                    const DataMQ::Descriptor& dataMQ,
                    const StatusMQ::Descriptor& statusMQ,
                    const ThreadInfo& halThreadInfo) {
                retval = r;
                if (retval == Result::OK) {
                    tempCommandMQ.reset(new CommandMQ(commandMQ));
                    tempDataMQ.reset(new DataMQ(dataMQ));
                    tempStatusMQ.reset(new StatusMQ(statusMQ));
                    if (tempDataMQ->isValid() && tempDataMQ->getEventFlagWord()) {
                        EventFlag::createEventFlag(tempDataMQ->getEventFlagWord(), &mEfGroup);
                    }
                    halThreadPid = halThreadInfo.pid;
                    halThreadTid = halThreadInfo.tid;
                }
            });
    if (!ret.isOk() || retval != Result::OK) {
        return processReturn("prepareForWriting", ret, retval);
    }
    if (!tempCommandMQ || !tempCommandMQ->isValid() ||
            !tempDataMQ || !tempDataMQ->isValid() ||
            !tempStatusMQ || !tempStatusMQ->isValid() ||
            !mEfGroup) {
        ALOGE_IF(!tempCommandMQ, "Failed to obtain command message queue for writing");
        ALOGE_IF(tempCommandMQ && !tempCommandMQ->isValid(),
                "Command message queue for writing is invalid");
        ALOGE_IF(!tempDataMQ, "Failed to obtain data message queue for writing");
        ALOGE_IF(tempDataMQ && !tempDataMQ->isValid(), "Data message queue for writing is invalid");
        ALOGE_IF(!tempStatusMQ, "Failed to obtain status message queue for writing");
        ALOGE_IF(tempStatusMQ && !tempStatusMQ->isValid(),
                "Status message queue for writing is invalid");
        ALOGE_IF(!mEfGroup, "Event flag creation for writing failed");
        return NO_INIT;
    }
    requestHalThreadPriority(halThreadPid, halThreadTid);

    mCommandMQ = std::move(tempCommandMQ);
    mDataMQ = std::move(tempDataMQ);
    mStatusMQ = std::move(tempStatusMQ);
    mWriterClient = gettid();
    return OK;
}

status_t StreamOutHalHidl::getRenderPosition(uint32_t *dspFrames) {
    if (mStream == 0) return NO_INIT;
    Result retval;
    Return<void> ret = mStream->getRenderPosition(
            [&](Result r, uint32_t d) {
                retval = r;
                if (retval == Result::OK) {
                    *dspFrames = d;
                }
            });
    return processReturn("getRenderPosition", ret, retval);
}

status_t StreamOutHalHidl::getNextWriteTimestamp(int64_t *timestamp) {
    if (mStream == 0) return NO_INIT;
    Result retval;
    Return<void> ret = mStream->getNextWriteTimestamp(
            [&](Result r, int64_t t) {
                retval = r;
                if (retval == Result::OK) {
                    *timestamp = t;
                }
            });
    return processReturn("getRenderPosition", ret, retval);
}

status_t StreamOutHalHidl::setCallback(wp<StreamOutHalInterfaceCallback> callback) {
    if (mStream == 0) return NO_INIT;
    status_t status = processReturn(
            "setCallback", mStream->setCallback(new StreamOutCallback(this)));
    if (status == OK) {
        mCallback = callback;
    }
    return status;
}

status_t StreamOutHalHidl::supportsPauseAndResume(bool *supportsPause, bool *supportsResume) {
    if (mStream == 0) return NO_INIT;
    Return<void> ret = mStream->supportsPauseAndResume(
            [&](bool p, bool r) {
                *supportsPause = p;
                *supportsResume = r;
            });
    return processReturn("supportsPauseAndResume", ret);
}

status_t StreamOutHalHidl::pause() {
    if (mStream == 0) return NO_INIT;
    return processReturn("pause", mStream->pause());
}

status_t StreamOutHalHidl::resume() {
    if (mStream == 0) return NO_INIT;
    return processReturn("pause", mStream->resume());
}

status_t StreamOutHalHidl::supportsDrain(bool *supportsDrain) {
    if (mStream == 0) return NO_INIT;
    return processReturn("supportsDrain", mStream->supportsDrain(), supportsDrain);
}

status_t StreamOutHalHidl::drain(bool earlyNotify) {
    if (mStream == 0) return NO_INIT;
    return processReturn(
            "drain", mStream->drain(earlyNotify ? AudioDrain::EARLY_NOTIFY : AudioDrain::ALL));
}

status_t StreamOutHalHidl::flush() {
    if (mStream == 0) return NO_INIT;
    return processReturn("pause", mStream->flush());
}

status_t StreamOutHalHidl::getPresentationPosition(uint64_t *frames, struct timespec *timestamp) {
    if (mStream == 0) return NO_INIT;
    if (mWriterClient == gettid() && mCommandMQ) {
        return callWriterThread(
                WriteCommand::GET_PRESENTATION_POSITION, "getPresentationPosition", nullptr, 0,
                [&](const WriteStatus& writeStatus) {
                    *frames = writeStatus.reply.presentationPosition.frames;
                    timestamp->tv_sec = writeStatus.reply.presentationPosition.timeStamp.tvSec;
                    timestamp->tv_nsec = writeStatus.reply.presentationPosition.timeStamp.tvNSec;
                });
    } else {
        Result retval;
        Return<void> ret = mStream->getPresentationPosition(
                [&](Result r, uint64_t hidlFrames, const TimeSpec& hidlTimeStamp) {
                    retval = r;
                    if (retval == Result::OK) {
                        *frames = hidlFrames;
                        timestamp->tv_sec = hidlTimeStamp.tvSec;
                        timestamp->tv_nsec = hidlTimeStamp.tvNSec;
                    }
                });
        return processReturn("getPresentationPosition", ret, retval);
    }
}

void StreamOutHalHidl::onWriteReady() {
    sp<StreamOutHalInterfaceCallback> callback = mCallback.promote();
    if (callback == 0) return;
    ALOGV("asyncCallback onWriteReady");
    callback->onWriteReady();
}

void StreamOutHalHidl::onDrainReady() {
    sp<StreamOutHalInterfaceCallback> callback = mCallback.promote();
    if (callback == 0) return;
    ALOGV("asyncCallback onDrainReady");
    callback->onDrainReady();
}

void StreamOutHalHidl::onError() {
    sp<StreamOutHalInterfaceCallback> callback = mCallback.promote();
    if (callback == 0) return;
    ALOGV("asyncCallback onError");
    callback->onError();
}


StreamInHalHidl::StreamInHalHidl(const sp<IStreamIn>& stream)
        : StreamHalHidl(stream.get()), mStream(stream), mReaderClient(0), mEfGroup(nullptr) {
}

StreamInHalHidl::~StreamInHalHidl() {
    if (mStream != 0) {
        processReturn("close", mStream->close());
        mStream.clear();
        hardware::IPCThreadState::self()->flushCommands();
    }
    if (mEfGroup) {
        EventFlag::deleteEventFlag(&mEfGroup);
    }
}

status_t StreamInHalHidl::getFrameSize(size_t *size) {
    if (mStream == 0) return NO_INIT;
    return processReturn("getFrameSize", mStream->getFrameSize(), size);
}

status_t StreamInHalHidl::setGain(float gain) {
    if (mStream == 0) return NO_INIT;
    return processReturn("setGain", mStream->setGain(gain));
}

//---------------------------------------------------------------
//添加获取音频控制信息参数方法
status_t StreamInHalHidl::setAudioControlConfiguration(String8 audioControlConfiguration){
    //私有变量mAudioControlConfig保存传进来的参数
    mAudioControlConfig = audioControlConfiguration;
    //利用3个char字符保存音频控制配置，若为字符’1‘则进行控制处理，若为字符’0‘则不对该维度进行控制
    //highFreqCtrl代表高频信息控制标志
    //voicePrintCtrl为声纹混淆控制
    //sodCtrl为麦克风、扬声器并发控制
    highFreqCtrl = mAudioControlConfig.string()[0];
    voicePrintCtrl = mAudioControlConfig.string()[1];
    sodCtrl = mAudioControlConfig.string()[2];

    if(highFreqCtrl == '1'){
    	highFreqControl = true;
    	LOGD("StreamInHalHidl---------highFreqControl设为true");
    }
    if(voicePrintCtrl == '1'){
    	voicePrintControl = true;
    	LOGD("StreamInHalHidl---------voicePrintControl设为true");
    }

    if(sodCtrl == '1'){
    	sodControl = true;
    	LOGD("sodControl---------sodControl设为true");
    }
    return NO_ERROR;
}
//---------------------------------------------------------------


status_t StreamInHalHidl::read(void *buffer, size_t bytes, size_t *read) {
    if (mStream == 0) return NO_INIT;
    *read = 0;

    if (bytes == 0 && !mDataMQ) {
        // Can't determine the size for the MQ buffer. Wait for a non-empty read request.
        return OK;
    }

    status_t status;
    if (!mDataMQ && (status = prepareForReading(bytes)) != OK) {
        return status;
    }

    ReadParameters params;
    params.command = ReadCommand::READ;
    params.params.read = bytes;
    status = callReaderThread(params, "read",
            [&](const ReadStatus& readStatus) {
                const size_t availToRead = mDataMQ->availableToRead();
                if (!mDataMQ->read(static_cast<uint8_t*>(buffer), std::min(bytes, availToRead))) {
                    ALOGE("data message queue read failed for \"read\"");
                }
                ALOGW_IF(availToRead != readStatus.reply.read,
                        "HAL read report inconsistent: mq = %d, status = %d",
                        (int32_t)availToRead, (int32_t)readStatus.reply.read);
                *read = readStatus.reply.read;
            });
    //get sampleRate of this record task
    int mySampleRate=mStream->getSampleRate();

     //initialization of pitchShifter
    float* floatBuffer = new float[bytes/2];

    if(voicePrintControl || highFreqControl){//高频滤波和声纹混淆处理均需要进行数据类型转换
        dataConverter=new MyConverter();
        floatBuffer=dataConverter->toFloatArray(static_cast<uint8_t*>(buffer),0,floatBuffer,0,bytes/2);
    }



    if(voicePrintControl){
        //instantiation of pitch shifting and initializing parameters.
        pitchShifter=new PitchShift(bytes/2);
        pitchShifter->setSampleRate(mySampleRate);
        pitchShifter->setFftFrameSize(128);
        pitchShifter->setOversampling(32);
        pitchShifter->setPitchShift(0.9f);
        //do pitch shift to audio stream data
        pitchShifter->smbPitchShift(floatBuffer,floatBuffer,0,bytes/2);
    }

    if(highFreqControl){
        lowPassFilter=new Butterworth();
        lowPassFilter->lowPassFiltering(floatBuffer,floatBuffer,bytes/2);
    }

    if(voicePrintControl || highFreqControl){
        buffer=dataConverter->toByteArray(floatBuffer,0,bytes/2,static_cast<uint8_t*>(buffer),0);
        delete[] floatBuffer;
        if(lowPassFilter!=NULL)
            delete lowPassFilter;
        if(pitchShifter!=NULL)
            delete pitchShifter;
    }

    highFreqControl = false;
    voicePrintControl =false;

    LOGD("bytes = %d------read = %d",(int)bytes,(int)*read);

    return status;
}

status_t StreamInHalHidl::callReaderThread(
        const ReadParameters& params, const char* cmdName,
        StreamInHalHidl::ReaderCallback callback) {
    if (!mCommandMQ->write(&params)) {
        ALOGW("command message queue write failed");
        return -EAGAIN;
    }
    mEfGroup->wake(static_cast<uint32_t>(MessageQueueFlagBits::NOT_FULL));

    // TODO: Remove manual event flag handling once blocking MQ is implemented. b/33815422
    uint32_t efState = 0;
retry:
    status_t ret = mEfGroup->wait(static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY), &efState);
    if (efState & static_cast<uint32_t>(MessageQueueFlagBits::NOT_EMPTY)) {
        ReadStatus readStatus;
        readStatus.retval = Result::NOT_INITIALIZED;
        if (!mStatusMQ->read(&readStatus)) {
            ALOGE("status message read failed for \"%s\"", cmdName);
        }
         if (readStatus.retval == Result::OK) {
            ret = OK;
            callback(readStatus);
        } else {
            ret = processReturn(cmdName, readStatus.retval);
        }
        return ret;
    }
    if (ret == -EAGAIN || ret == -EINTR) {
        // Spurious wakeup. This normally retries no more than once.
        goto retry;
    }
    return ret;
}

status_t StreamInHalHidl::prepareForReading(size_t bufferSize) {
    std::unique_ptr<CommandMQ> tempCommandMQ;
    std::unique_ptr<DataMQ> tempDataMQ;
    std::unique_ptr<StatusMQ> tempStatusMQ;
    Result retval;
    pid_t halThreadPid, halThreadTid;
    Return<void> ret = mStream->prepareForReading(
            1, bufferSize,
            [&](Result r,
                    const CommandMQ::Descriptor& commandMQ,
                    const DataMQ::Descriptor& dataMQ,
                    const StatusMQ::Descriptor& statusMQ,
                    const ThreadInfo& halThreadInfo) {
                retval = r;
                if (retval == Result::OK) {
                    tempCommandMQ.reset(new CommandMQ(commandMQ));
                    tempDataMQ.reset(new DataMQ(dataMQ));
                    tempStatusMQ.reset(new StatusMQ(statusMQ));
                    if (tempDataMQ->isValid() && tempDataMQ->getEventFlagWord()) {
                        EventFlag::createEventFlag(tempDataMQ->getEventFlagWord(), &mEfGroup);
                    }
                    halThreadPid = halThreadInfo.pid;
                    halThreadTid = halThreadInfo.tid;
                }
            });
    if (!ret.isOk() || retval != Result::OK) {
        return processReturn("prepareForReading", ret, retval);
    }
    if (!tempCommandMQ || !tempCommandMQ->isValid() ||
            !tempDataMQ || !tempDataMQ->isValid() ||
            !tempStatusMQ || !tempStatusMQ->isValid() ||
            !mEfGroup) {
        ALOGE_IF(!tempCommandMQ, "Failed to obtain command message queue for writing");
        ALOGE_IF(tempCommandMQ && !tempCommandMQ->isValid(),
                "Command message queue for writing is invalid");
        ALOGE_IF(!tempDataMQ, "Failed to obtain data message queue for reading");
        ALOGE_IF(tempDataMQ && !tempDataMQ->isValid(), "Data message queue for reading is invalid");
        ALOGE_IF(!tempStatusMQ, "Failed to obtain status message queue for reading");
        ALOGE_IF(tempStatusMQ && !tempStatusMQ->isValid(),
                "Status message queue for reading is invalid");
        ALOGE_IF(!mEfGroup, "Event flag creation for reading failed");
        return NO_INIT;
    }
    requestHalThreadPriority(halThreadPid, halThreadTid);

    mCommandMQ = std::move(tempCommandMQ);
    mDataMQ = std::move(tempDataMQ);
    mStatusMQ = std::move(tempStatusMQ);
    mReaderClient = gettid();
    return OK;
}

status_t StreamInHalHidl::getInputFramesLost(uint32_t *framesLost) {
    if (mStream == 0) return NO_INIT;
    return processReturn("getInputFramesLost", mStream->getInputFramesLost(), framesLost);
}

status_t StreamInHalHidl::getCapturePosition(int64_t *frames, int64_t *time) {
    if (mStream == 0) return NO_INIT;
    if (mReaderClient == gettid() && mCommandMQ) {
        ReadParameters params;
        params.command = ReadCommand::GET_CAPTURE_POSITION;
        return callReaderThread(params, "getCapturePosition",
                [&](const ReadStatus& readStatus) {
                    *frames = readStatus.reply.capturePosition.frames;
                    *time = readStatus.reply.capturePosition.time;
                });
    } else {
        Result retval;
        Return<void> ret = mStream->getCapturePosition(
                [&](Result r, uint64_t hidlFrames, uint64_t hidlTime) {
                    retval = r;
                    if (retval == Result::OK) {
                        *frames = hidlFrames;
                        *time = hidlTime;
                    }
                });
        return processReturn("getCapturePosition", ret, retval);
    }
}




//add pitchShifter class and its method implement here
PitchShift::PitchShift(int maxFrameSize) {
    MAX_FRAME_LENGTH=maxFrameSize;
    gInFIFO=new float[MAX_FRAME_LENGTH];
    gOutFIFO=new float[MAX_FRAME_LENGTH];
    gFFTworksp=new float[MAX_FRAME_LENGTH];
    gLastPhase=new float[MAX_FRAME_LENGTH];
    gSumPhase=new float[MAX_FRAME_LENGTH];
    gOutputAccum=new float[MAX_FRAME_LENGTH];
    gAnaFreq=new float[MAX_FRAME_LENGTH];
    gAnaMagn=new float[MAX_FRAME_LENGTH];
    gSynFreq=new float[MAX_FRAME_LENGTH];
    gSynMagn=new float[MAX_FRAME_LENGTH];
}

int PitchShift::getMaxFrameLength(){
    return MAX_FRAME_LENGTH;
}

int PitchShift:: getSampleRate(){
    return (int)sampleRate;
}

void PitchShift::setSampleRate(int Rate){
    sampleRate=Rate;
}

int PitchShift::getFftFrameSize(){
    return fftFrameSize;
}

void PitchShift::setFftFrameSize(int fftSize){
    fftFrameSize = fftSize;

}


int PitchShift::getOversampling(){
    return overSampling;

}

void PitchShift::setOversampling(int oversampSize){
    overSampling=oversampSize;

}

float PitchShift::getPitchShift(){
    return pitchShift;

}

void PitchShift::setPitchShift(float factor){
    pitchShift=factor;

}

void PitchShift::smbPitchShift(float* indata,float* outdata,int offset, int numSampsToProcess){
    LOGD("smbPitchShift---------doing pitch shift to indata");
    int gRover = 0;
    double magn, phase, tmp, window, real, imag;
    double freqPerBin, expct;
    int i,k, qpd, index, inFifoLatency, stepSize, fftFrameSize2;

    /* 为了方便计算,设置一些辅助变量*/
    fftFrameSize2 = fftFrameSize/2;
    stepSize = fftFrameSize/overSampling;
    freqPerBin = sampleRate/(double)fftFrameSize;
    expct = 2.*M_PI*(double)stepSize/(double)fftFrameSize;
    inFifoLatency = fftFrameSize-stepSize;
    if (gRover == 0) gRover = inFifoLatency;

    /* 循环处理 */
    for (i = 0; i < numSampsToProcess; i++){

        /* 若未读入足够数据，则继续读入数据*/
        gInFIFO[gRover] = (float)(indata[i+offset]);
        outdata[i+offset] = gOutFIFO[gRover-inFifoLatency];
        gRover++;

        /* 已有足够多的数据,进行数据 */
        if (gRover >= fftFrameSize) {
            gRover = inFifoLatency;


            for (k = 0; k < fftFrameSize;k++) {
                window = -.5*cos(2.*M_PI*(double)k/(double)fftFrameSize)+.5;
                gFFTworksp[2*k] = (float) (gInFIFO[k] * window);
                gFFTworksp[2*k+1] = 0.f;
            }


            /* ***************** ANALYSIS ******************* */
            /* 进行傅里叶变换 */
            smbFft(gFFTworksp, fftFrameSize, -1);

            /* 分析步骤 */
            for (k = 0; k <= fftFrameSize2; k++) {

                /* de-interlace FFT buffer */
                real = gFFTworksp[2*k];
                imag = gFFTworksp[2*k+1];

                /* 计算幅度和相位 */
                magn = 2.*sqrt(real*real + imag*imag);
                phase = smbAtan2(imag,real);

                /* 计算相位差 */
                tmp = phase - gLastPhase[k];
                gLastPhase[k] = (float) phase;

                /* 减去预期的相位差 */
                tmp -= (double)k*expct;

                /* 将 delta 相映射到 +/- Pi 区间 */
                qpd = (int) (tmp/M_PI);
                if (qpd >= 0) qpd += qpd&1;
                else qpd -= qpd&1;
                tmp -= M_PI*(double)qpd;

                /* 从窗口频率得到 +/- Pi 区间的偏差 */
                tmp = overSampling*tmp/(2.*M_PI);

                /* 计算第k部分的真正频率 */
                tmp = (double)k*freqPerBin + tmp*freqPerBin;

                /* 将幅度和真实频率保存在分析数组 */
                gAnaMagn[k] = (float) magn;
                gAnaFreq[k] = (float) tmp;

            }

            /* ***************** PROCESSING ******************* */
            /* 该部分进行真正的变调处理 */
            memset(gSynMagn, 0, fftFrameSize*sizeof(float));
            //memset(gSynFreq, 0, fftFrameSize*sizeof(float));

            for (k = 0; k <= fftFrameSize2; k++) {
                index = (int) (k*pitchShift);
                if (index <= fftFrameSize2) {
                    gSynMagn[index] += gAnaMagn[k];
                    gSynFreq[index] = gAnaFreq[k] * pitchShift;
                }
            }

            /* ***************** SYNTHESIS ******************* */
            /* 进行合成 */
            for (k = 0; k <= fftFrameSize2; k++) {

                /*从拼接数组得到幅度和真实频率 */
                magn = gSynMagn[k];
                tmp = gSynFreq[k];

                /* 提取窗口中间频率 */
                tmp -= (double)k*freqPerBin;

                /* 从频率偏差得到窗口偏差 */
                tmp /= freqPerBin;

                /* 考虑过采样因子的影响 */
                tmp = 2.*M_PI*tmp/overSampling;

                /* 提前将重叠相位加进来 */
                tmp += (double)k*expct;

                /* 累加delta相位,得到窗口相位 */
                gSumPhase[k] += tmp;
                phase = gSumPhase[k];

                /* 得到实部和虚部,并进行交错 */
                gFFTworksp[2*k] = (float) (magn*cos(phase));
                gFFTworksp[2*k+1] = (float) (magn*sin(phase));
            }

            /* 将负频率置0 */
            for (k = fftFrameSize+2; k < 2*fftFrameSize; k++) gFFTworksp[k] = 0.f;

            /* 进行逆傅里叶变换 */
            smbFft(gFFTworksp, fftFrameSize, 1);

            /* 做窗内处理并加到累加器中 */
            for(k=0; k < fftFrameSize; k++) {
                window = -.5*cos(2.*M_PI*(double)k/(double)fftFrameSize)+.5;
                gOutputAccum[k] += 2.*window*gFFTworksp[2*k]/(fftFrameSize2*overSampling);
            }
            for (k = 0; k < stepSize; k++) gOutFIFO[k] = gOutputAccum[k];

            /* 变调累加器 */
            //System.arraycopy(gOutputAccum, stepSize, gOutputAccum, 0, fftFrameSize);
            memmove(gOutputAccum, gOutputAccum+stepSize, fftFrameSize*sizeof(float));

            /* 按FIFO原则移动窗口 */
            for (k = 0; k < inFifoLatency; k++) gInFIFO[k] = gInFIFO[k+stepSize];
        }
    }
}

void PitchShift::smbFft(float* fftBuffer, int fftFrameSize, int sign){
    float wr, wi, arg, temp;
    float tr, ti, ur, ui;
    int i, bitm, j, le, le2, k;

    int p1, p2, p1r, p1i, p2r, p2i;

    for (i = 2; i < 2*fftFrameSize-2; i += 2) {
        for (bitm = 2, j = 0; bitm < 2*fftFrameSize; bitm <<= 1) {
            if ((i & bitm)!=0) j++;
            j <<= 1;
        }
        if (i < j) {
            //p1 = fftBuffer+i;
            p1=i;
            //p2 = fftBuffer+j;
            p2=j;
            //temp = *p1;
            temp = fftBuffer[p1];
            //*(p1++) = *p2;
            fftBuffer[p1++]=fftBuffer[p2];
            //*(p2++) = temp;
            fftBuffer[p2++] = temp;
            //temp = *p1;
            temp = fftBuffer[p1];
            //*p1 = *p2;
            fftBuffer[p1]=fftBuffer[p2];
            //*p2 = temp;
            fftBuffer[p2]=temp;
        }
    }
    for (k = 0, le = 2; k < (long)(log(fftFrameSize)/log(2.)+.5); k++) {
        le <<= 1;
        le2 = le>>1;
        ur = 1.0f;
        ui = 0.0f;
        arg = (float) (M_PI / (le2>>1));
        wr = (float) cos(arg);
        wi = (float) (sign*sin(arg));
        for (j = 0; j < le2; j += 2) {
            //p1r = fftBuffer+j;
            p1r = j;
            p1i = p1r+1;
            p2r = p1r+le2;
            p2i = p2r+1;
            for (i = j; i < 2*fftFrameSize; i += le) {
                tr = fftBuffer[p2r] * ur - fftBuffer[p2i] * ui;
                ti = fftBuffer[p2r] * ui + fftBuffer[p2i] * ur;
                fftBuffer[p2r] = fftBuffer[p1r] - tr;
                fftBuffer[p2i] = fftBuffer[p1i] - ti;
                fftBuffer[p1r] += tr;
                fftBuffer[p1i] += ti;
                p1r += le; p1i += le;
                p2r += le; p2i += le;
            }
            tr = ur*wr - ui*wi;
            ui = ur*wi + ui*wr;
            ur = tr;
        }
    }

}


double PitchShift::smbAtan2(double x, double y){
    double signx;
    if (x > 0.) signx = 1.;
    else signx = -1.;

    if (x == 0.) return 0.;
    if (y == 0.) return signx * M_PI / 2.;

    return atan2(x, y);

}



//a data type converter
MyConverter::MyConverter(){
    //default constructor of MyConverter
}

float* MyConverter::toFloatArray(uint8_t* in_buff, int in_offset, float* out_buff, int out_offset, int out_len) {
    int ix = in_offset;
    int len = out_offset + out_len;
    for (int ox = out_offset; ox < len; ox++) {
        int paramOne=(in_buff[ix++] & 0xFF);
        out_buff[ox] = ((short) ( paramOne |
        (in_buff[ix++] << 8))) * (1.0f / 32767.0f);
    }
    LOGD("MyConverter----char------------>>>float");
        return out_buff;
}


uint8_t* MyConverter::toByteArray(float *in_buff, int in_offset, int in_len, uint8_t *out_buff, int out_offset) {
    int ox = out_offset;
    int len = in_offset + in_len;
    for (int ix = in_offset; ix < len; ix++) {
        int x = (int) (in_buff[ix] * 32767.0);
        out_buff[ox++] = (uint8_t) x;
        out_buff[ox++] = (uint8_t) (x >> 8);
    }
    LOGD("MyConverter----float------------>>>char");
    return out_buff;
}


//Butterworth low pass filter, which cut off frequency is 0.1*sampleRate

Butterworth::Butterworth(){
    //do nothing
}

void Butterworth::lowPassFiltering(float* src,float* des,int size){    
    const int NZEROS = 6;
    const int NPOLES = 6;
    const double GAIN = 2.936532839e+03;

    double xv[NZEROS+1] = {0.0}, yv[NPOLES+1] = {0.0};

    for (int i = 0; i < size; i++)
    {
        xv[0] = xv[1]; xv[1] = xv[2]; xv[2] = xv[3]; xv[3] = xv[4]; xv[4] = xv[5]; xv[5] = xv[6];
        xv[6] = src[i] / GAIN;
        yv[0] = yv[1]; yv[1] = yv[2]; yv[2] = yv[3]; yv[3] = yv[4]; yv[4] = yv[5]; yv[5] = yv[6];
        yv[6] =   (xv[0] + xv[6]) + 6.0 * (xv[1] + xv[5]) + 15.0 * (xv[2] + xv[4])
                  + 20.0 * xv[3]
                  + ( -0.0837564796 * yv[0]) + (  0.7052741145 * yv[1])
                  + ( -2.5294949058 * yv[2]) + (  4.9654152288 * yv[3])
                  + ( -5.6586671659 * yv[4]) + (  3.5794347983 * yv[5]);
        des[i] = yv[6];
    }
    LOGD("------------ Butterworth low pass filtering");
}


} // namespace android
