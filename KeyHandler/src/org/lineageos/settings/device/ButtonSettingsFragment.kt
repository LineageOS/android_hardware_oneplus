/*
 * Copyright (C) 2015 The CyanogenMod Project
 *               2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.settings.device

import android.os.Bundle;
import android.view.MenuItem
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceGroup
import androidx.preference.PreferenceFragment

class ButtonSettingsFragment : PreferenceFragment() {
    
    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        addPreferencesFromResource(R.xml.button_panel)
        getActivity().getActionBar().setDisplayHomeAsUpEnabled(true)
    }

    override fun addPreferencesFromResource(preferencesResId: Int) {
        super.addPreferencesFromResource(preferencesResId)
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            R.id.home -> {
                getActivity().finish()
                return true
            }
        }
        return super.onOptionsItemSelected(item)
    }

    private fun removePref(pref: Preference) {
        val parent: PreferenceGroup = pref.getParent() ?: return
        parent.removePreference(pref)
        if (parent.getPreferenceCount() === 0) {
            removePref(parent)
        }
    }
}