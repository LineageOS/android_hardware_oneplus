/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pocketmode

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.util.Log
import org.lineageos.pocketmode.R

import java.io.File
import java.util.concurrent.Executors

class PocketSensor(context: Context, sensorType: String, private val sensorValue: Float) :
    SensorEventListener {
    private val sensorManager = context.getSystemService(SensorManager::class.java)
    private val sensor = sensorManager.getSensorList(Sensor.TYPE_ALL).find {
        it.stringType == sensorType
    }

    private val executorService = Executors.newSingleThreadExecutor()
    private val fingerprintDisablePaths =
        context.resources.getStringArray(R.array.fingerprint_disable_paths)

    override fun onSensorChanged(event: SensorEvent) {
        setFingerprintEnabled(event.values[0] == sensorValue)
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    private fun setFingerprintEnabled(enabled: Boolean) {
        fingerprintDisablePaths.forEach {
            val file = File(it)
            if (file.canWrite()) {
                file.writeText(if (enabled) "1" else "0")
            }
        }
    }

    fun enable() {
        if (sensor != null) {
            Log.d(TAG, "Enabling")
            executorService.submit {
                sensorManager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_NORMAL)
            }
        }
    }

    fun disable() {
        if (sensor != null) {
            Log.d(TAG, "Disabling")
            executorService.submit {
                sensorManager.unregisterListener(this, sensor)
                setFingerprintEnabled(true)
            }
        }
    }

    companion object {
        private const val TAG = "PocketSensor"
    }
}
