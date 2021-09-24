/*
 * Copyright (c) 2016 The CyanogenMod Project
 *               2018-2021 The LineageOS Project
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
package org.lineageos.pocketmode

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorManager
import android.hardware.SensorEventListener
import android.os.FileUtils
import android.util.Log

import java.io.IOException
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

class ProximitySensor(private val mContext: Context) : SensorEventListener {
    private var FPC_FILE: String? = null

    private val mSensorManager = mContext.getSystemService(SensorManager::class.java)
    private val mSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_PROXIMITY)
    private val mExecutorService: ExecutorService

    override fun onSensorChanged(event: SensorEvent) {
        setFPProximityState(event.values.get(0) < mSensor.maximumRange)
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {
        /* Empty */
    }

    private fun setFPProximityState(isNear: Boolean) {
        try {
            FileUtils.stringToFile(FPC_FILE, if (isNear) "1" else "0")
        } catch (e: IOException) {
            Log.e(TAG, "Failed to write to $FPC_FILE", e)
        }
    }

    public fun enable() {
        if (DEBUG) Log.d(TAG, "Enabling")
        mExecutorService.submit {
            mSensorManager.registerListener(
                    this, mSensor, SensorManager.SENSOR_DELAY_NORMAL)
        }
    }

    public fun disable() {
        if (DEBUG) Log.d(TAG, "Disabling")
        mExecutorService.submit {
            mSensorManager.unregisterListener(this, mSensor)
            // Ensure FP is left enabled
            setFPProximityState( /* isNear */false)
        }
    }

    companion object {
        private const val DEBUG = false
        private const val TAG = "PocketModeProximity"
    }

    init {
        mExecutorService = Executors.newSingleThreadExecutor()
        when (SystemProperties.get("ro.lineage.device", "")) {
            "cheeseburger" -> FPC_FILE =
                    "/sys/devices/soc/soc:fpc_fpc1020/proximity_state"
            "dumpling" -> FPC_FILE =
                    "/sys/devices/soc/soc:goodix_fp/proximity_state"
            else -> {
                FPC_FILE = ""
                Log.e(TAG, "Device model for proximity state file not found!")
            }
        }
    }
}
