/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.pocketmode

import android.app.Service
import android.content.IntentFilter
import android.content.Intent
import android.os.IBinder
import android.content.BroadcastReceiver
import android.content.Context
import android.util.Log

class PocketModeService : Service() {
    private lateinit var pocketSensor: PocketSensor

    private val screenStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                Intent.ACTION_SCREEN_ON -> onDisplayOn()
                Intent.ACTION_SCREEN_OFF -> onDisplayOff()
            }
        }
    }

    override fun onCreate() {
        Log.d(TAG, "Creating service")
        pocketSensor = PocketSensor(this, "oneplus.sensor.pocket")

        val screenStateFilter = IntentFilter()
        screenStateFilter.addAction(Intent.ACTION_SCREEN_ON)
        screenStateFilter.addAction(Intent.ACTION_SCREEN_OFF)
        registerReceiver(screenStateReceiver, screenStateFilter)
    }

    override fun onStartCommand(intent: Intent, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()

        unregisterReceiver(screenStateReceiver)
        pocketSensor.disable()
    }

    override fun onBind(intent: Intent): IBinder? = null

    private fun onDisplayOn() {
        pocketSensor.disable()
    }

    private fun onDisplayOff() {
        pocketSensor.enable()
    }

    companion object {
        private const val TAG = "PocketModeService"
    }
}
