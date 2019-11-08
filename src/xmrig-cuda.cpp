/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2018 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2018-2020 SChernykh   <https://github.com/SChernykh>
 * Copyright 2016-2020 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "cryptonight.h"
#include "cuda_device.hpp"
#include "version.h"
#include "xmrig-cuda.h"


#include <map>
#include <mutex>
#include <string>
#include <cuda_runtime_api.h>


static std::mutex mutex;


class DatasetHost
{
public:
    inline const void *reg(const void *dataset, size_t size)
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (!m_ptr) {
            m_ptr = const_cast<void *>(dataset);
            CUDA_CHECK(0, cudaHostRegister(m_ptr, size, cudaHostRegisterPortable | cudaHostRegisterMapped));
        }

        ++m_refs;

        return m_ptr;
    }


    inline void release()
    {
        std::lock_guard<std::mutex> lock(mutex);

        --m_refs;

        if (m_refs == 0) {
            cudaHostUnregister(m_ptr);
        }
    }

    int32_t m_refs  = 0;
    void *m_ptr     = nullptr;
};


static std::map<int, std::string> errors;
static DatasetHost datasetHost;

static const char *kUnsupportedAlgorithm = "Unsupported algorithm";


static inline void saveError(int id, std::exception &ex)
{
    std::lock_guard<std::mutex> lock(mutex);
    errors[id] = ex.what();
}


static inline void saveError(int id, const char *error)
{
    std::lock_guard<std::mutex> lock(mutex);
    errors[id] = error;
}


static inline void resetError(int id)
{
    std::lock_guard<std::mutex> lock(mutex);
    errors.erase(id);
}


extern "C" {


bool cnHash(nvid_ctx *ctx, uint32_t startNonce, uint64_t height, uint64_t target, uint32_t *rescount, uint32_t *resnonce)
{
    resetError(ctx->device_id);

    try {
        cryptonight_extra_cpu_prepare(ctx, startNonce, ctx->algorithm);
        cryptonight_gpu_hash(ctx, ctx->algorithm, height, startNonce);
        cryptonight_extra_cpu_final(ctx, startNonce, target, rescount, resnonce, ctx->algorithm);
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool deviceInfo_v2(nvid_ctx *ctx, int32_t blocks, int32_t threads, const char *algo, int32_t dataset_host)
{
    using namespace xmrig;

    if (algo != nullptr) {
        ctx->algorithm = algo;

        if (!ctx->algorithm.isValid()) {
            saveError(ctx->device_id, kUnsupportedAlgorithm);

            return false;
        }
    }

    ctx->device_blocks   = blocks;
    ctx->device_threads  = threads;
    ctx->rx_dataset_host = dataset_host;

    return cuda_get_deviceinfo(ctx) == 0;
}


bool deviceInit(nvid_ctx *ctx)
{
    resetError(ctx->device_id);

    if (ctx == nullptr) {
        return false;
    }

    int rc = 0;

    try {
        rc = cryptonight_gpu_init(ctx);
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return rc;
}


bool rxHash(nvid_ctx *ctx, uint32_t startNonce, uint64_t target, uint32_t *rescount, uint32_t *resnonce)
{
    resetError(ctx->device_id);

    try {
        switch (ctx->algorithm.id()) {
#       ifdef XMRIG_ALGO_RANDOMX
        case xmrig::Algorithm::RX_0:
        case xmrig::Algorithm::RX_SFX:
            RandomX_Monero::hash(ctx, startNonce, target, rescount, resnonce, ctx->rx_batch_size);
            break;

        case xmrig::Algorithm::RX_WOW:
            RandomX_Wownero::hash(ctx, startNonce, target, rescount, resnonce, ctx->rx_batch_size);
            break;

        case xmrig::Algorithm::RX_LOKI:
            RandomX_Loki::hash(ctx, startNonce, target, rescount, resnonce, ctx->rx_batch_size);
            break;

        case xmrig::Algorithm::RX_ARQ:
            RandomX_Arqma::hash(ctx, startNonce, target, rescount, resnonce, ctx->rx_batch_size);
            break;

        case xmrig::Algorithm::RX_KEVA:
            RandomX_Keva::hash(ctx, startNonce, target, rescount, resnonce, ctx->rx_batch_size);
            break;
#       endif

        default:
            throw std::runtime_error(kUnsupportedAlgorithm);
        }
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool rxPrepare(nvid_ctx *ctx, const void *dataset, size_t datasetSize, bool, uint32_t batchSize)
{
    resetError(ctx->device_id);

    try {
#       ifdef XMRIG_ALGO_RANDOMX
        randomx_prepare(ctx, ctx->rx_dataset_host > 0 ? datasetHost.reg(dataset, datasetSize) : dataset, datasetSize, batchSize);
#       else
        throw std::runtime_error(kUnsupportedAlgorithm);
#       endif
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool astroBWTHash(nvid_ctx *ctx, uint32_t startNonce, uint64_t target, uint32_t *rescount, uint32_t *resnonce)
{
    resetError(ctx->device_id);

    try {
        switch (ctx->algorithm.id()) {
#       ifdef XMRIG_ALGO_ASTROBWT
        case xmrig::Algorithm::ASTROBWT_DERO:
            AstroBWT_Dero::hash(ctx, startNonce, target, rescount, resnonce);
            break;
#       endif

        default:
            throw std::runtime_error(kUnsupportedAlgorithm);
        }
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool astroBWTPrepare(nvid_ctx *ctx, uint32_t batchSize)
{
    resetError(ctx->device_id);

    try {
#       ifdef XMRIG_ALGO_ASTROBWT
        astrobwt_prepare(ctx, batchSize);
#       else
        throw std::runtime_error(kUnsupportedAlgorithm);
#       endif
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool kawPowHash(nvid_ctx *ctx, uint8_t* job_blob, uint64_t target, uint32_t *rescount, uint32_t *resnonce, uint32_t *skipped_hashes)
{
    resetError(ctx->device_id);

    try {
        switch (ctx->algorithm.id()) {
#       ifdef XMRIG_ALGO_KAWPOW
        case xmrig::Algorithm::KAWPOW_RVN:
            KawPow_Raven::hash(ctx, job_blob, target, rescount, resnonce, skipped_hashes);
            break;
#       endif

        default:
            throw std::runtime_error(kUnsupportedAlgorithm);
        }
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool kawPowPrepare(nvid_ctx *ctx, const void* cache, size_t cache_size, size_t dag_size, uint32_t height, const uint64_t* dag_sizes)
{
    resetError(ctx->device_id);

    try {
#       ifdef XMRIG_ALGO_KAWPOW
        kawpow_prepare(ctx, cache, cache_size, nullptr, dag_size, height, dag_sizes);
#       else
        throw std::runtime_error(kUnsupportedAlgorithm);
#       endif
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool kawPowPrepare_v2(nvid_ctx *ctx, const void* cache, size_t cache_size, const void* dag_precalc, size_t dag_size, uint32_t height, const uint64_t* dag_sizes)
{
    resetError(ctx->device_id);

    try {
#       ifdef XMRIG_ALGO_KAWPOW
        kawpow_prepare(ctx, cache, cache_size, dag_precalc, dag_size, height, dag_sizes);
#       else
        throw std::runtime_error(kUnsupportedAlgorithm);
#       endif
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool kawPowStopHash(nvid_ctx *ctx)
{
    try {
#       ifdef XMRIG_ALGO_KAWPOW
        kawpow_stop_hash(ctx);
#       else
        throw std::runtime_error(kUnsupportedAlgorithm);
#       endif
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool setJob_v2(nvid_ctx *ctx, const void *data, size_t size, const char *algo)
{
    if (ctx == nullptr) {
        return false;
    }

    resetError(ctx->device_id);
    ctx->algorithm = algo;

    if (!ctx->algorithm.isValid()) {
        saveError(ctx->device_id, kUnsupportedAlgorithm);

        return false;
    }

    try {
        const xmrig::Algorithm::Family f = xmrig::Algorithm::family(ctx->algorithm);

        if ((f == xmrig::Algorithm::RANDOM_X) || (f == xmrig::Algorithm::ASTROBWT)) {
            cuda_extra_cpu_set_data(ctx, data, size);
        }
        else {
            cryptonight_extra_cpu_set_data(ctx, data, size);
        }
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


bool setJob(nvid_ctx *ctx, const void *data, size_t size, int32_t algo)
{
    if (ctx == nullptr) {
        return false;
    }

    resetError(ctx->device_id);
    ctx->algorithm = algo;

    try {
        const xmrig::Algorithm::Family f = xmrig::Algorithm::family(ctx->algorithm);

        if ((f == xmrig::Algorithm::RANDOM_X) || (f == xmrig::Algorithm::ASTROBWT)) {
            cuda_extra_cpu_set_data(ctx, data, size);
        }
        else {
            cryptonight_extra_cpu_set_data(ctx, data, size);
        }
    }
    catch (std::exception &ex) {
        saveError(ctx->device_id, ex);

        return false;
    }

    return true;
}


const char *deviceName(nvid_ctx *ctx)
{
    return ctx->device_name;
}


const char *lastError(nvid_ctx *ctx)
{
    std::lock_guard<std::mutex> lock(mutex);

    return errors.count(ctx->device_id) ? errors[ctx->device_id].c_str() : nullptr;
}


const char *pluginVersion()
{
    return APP_VERSION;
}


int32_t deviceInfo(nvid_ctx *ctx, int32_t blocks, int32_t threads, int32_t algo, int32_t dataset_host)
{
    ctx->algorithm       = algo;
    ctx->device_blocks   = blocks;
    ctx->device_threads  = threads;
    ctx->rx_dataset_host = dataset_host;

    return cuda_get_deviceinfo(ctx);
}


int32_t deviceInt(nvid_ctx *ctx, DeviceProperty property)
{
    if (ctx == nullptr) {
        return 0;
    }

    switch (property) {
    case DeviceId:
        return ctx->device_id;

    case DeviceAlgorithm:
        return ctx->algorithm;

    case DeviceArchMajor:
        return ctx->device_arch[0];

    case DeviceArchMinor:
        return ctx->device_arch[1];

    case DeviceSmx:
        return ctx->device_mpcount;

    case DeviceBlocks:
        return ctx->device_blocks;

    case DeviceThreads:
        return ctx->device_threads;

    case DeviceBFactor:
        return ctx->device_bfactor;

    case DeviceBSleep:
        return ctx->device_bsleep;

    case DeviceClockRate:
        return ctx->device_clockRate;

    case DeviceMemoryClockRate:
        return ctx->device_memoryClockRate;

    case DevicePciBusID:
        return ctx->device_pciBusID;

    case DevicePciDeviceID:
        return ctx->device_pciDeviceID;

    case DevicePciDomainID:
        return ctx->device_pciDomainID;

    case DeviceDatasetHost:
        return ctx->rx_dataset_host;

    case DeviceAstroBWTProcessedHashes:
        return static_cast<int32_t>(ctx->astrobwt_processed_hashes);

    default:
        break;
    }

    return 0;
}


nvid_ctx *alloc(uint32_t id, int32_t bfactor, int32_t bsleep)
{
    auto ctx = new nvid_ctx();

    ctx->device_id      = static_cast<int>(id);
    ctx->device_bfactor = bfactor;
    ctx->device_bsleep  = bsleep;

    return ctx;
}


uint32_t deviceCount()
{
    return static_cast<uint32_t>(cuda_get_devicecount());
}


uint32_t deviceUint(nvid_ctx *ctx, DeviceProperty property)
{
    return static_cast<uint32_t>(deviceInt(ctx, property));
}


uint32_t version(Version version)
{
    switch (version) {
    case ApiVersion:
        return API_VERSION;

    case DriverVersion:
        return static_cast<uint32_t>(cuda_get_driver_version());

    case RuntimeVersion:
        return static_cast<uint32_t>(cuda_get_runtime_version());
    }

    return 0;
}


uint64_t deviceUlong(nvid_ctx *ctx, DeviceProperty property)
{
    if (ctx == nullptr) {
        return 0;
    }

    switch (property) {
    case DeviceMemoryTotal:
        return ctx->device_memoryTotal;

    case DeviceMemoryFree:
        return ctx->device_memoryFree;

    default:
        break;
    }

    return 0;
}


void init()
{
    cuInit(0);
}


void release(nvid_ctx *ctx)
{
    if (ctx == nullptr) {
        return;
    }

    delete[] ctx->device_name;

    // cudaFree, cuModuleUnload, cuCtxDestroy check for nullptr internally

    cudaFree(ctx->d_input);
    cudaFree(ctx->d_result_count);
    cudaFree(ctx->d_result_nonce);
    cudaFree(ctx->d_long_state);
    cudaFree(ctx->d_ctx_state);
    cudaFree(ctx->d_ctx_state2);
    cudaFree(ctx->d_ctx_a);
    cudaFree(ctx->d_ctx_b);
    cudaFree(ctx->d_ctx_key1);
    cudaFree(ctx->d_ctx_key2);
    cudaFree(ctx->d_ctx_text);

    if (ctx->rx_dataset_host > 0) {
        datasetHost.release();
    }
    else {
        cudaFree(ctx->d_rx_dataset);
    }

    cudaFree(ctx->d_rx_hashes);
    cudaFree(ctx->d_rx_entropy);
    cudaFree(ctx->d_rx_vm_states);
    cudaFree(ctx->d_rx_rounding);

    cudaFree(ctx->astrobwt_salsa20_keys);
    cudaFree(ctx->astrobwt_bwt_data);
    cudaFree(ctx->astrobwt_bwt_data_sizes);
    cudaFree(ctx->astrobwt_indices);
    cudaFree(ctx->astrobwt_tmp_indices);
    cudaFree(ctx->astrobwt_filtered_hashes);
    cudaFree(ctx->astrobwt_shares);
    cudaFree(ctx->astrobwt_offsets_begin);
    cudaFree(ctx->astrobwt_offsets_end);

    cudaFree(ctx->kawpow_cache);
    cudaFree(ctx->kawpow_dag);
    cudaFreeHost(ctx->kawpow_stop_host);

    cuModuleUnload(ctx->module);
    cuModuleUnload(ctx->kawpow_module);

    cuCtxDestroy(ctx->cuContext);

    delete ctx;
}


}
