/*
 * Copyright (C) 2015 The CyanogenMod Project
 *               2017 The LineageOS Project
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
package org.lineageos.settings.device

import android.R
import android.view.MenuItem
import androidx.preference.ListPreference

class ButtonSettingsFragment : PreferenceFragment(), OnPreferenceChangeListener {
    fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        addPreferencesFromResource(R.xml.button_panel)
        getActivity().getActionBar().setDisplayHomeAsUpEnabled(true)
    }

    fun onPreferenceChange(preference: Preference, newValue: Any): Boolean {
        var node: String = Constants.sBooleanNodePreferenceMap.get(preference.getKey())
        if (!TextUtils.isEmpty(node) && FileUtils.isFileWritable(node)) {
            val value = newValue as Boolean
            FileUtils.writeLine(node, if (value) "1" else "0")
            return true
        }
        node = Constants.sStringNodePreferenceMap.get(preference.getKey())
        if (!TextUtils.isEmpty(node) && FileUtils.isFileWritable(node)) {
            FileUtils.writeLine(node, newValue as String)
            return true
        }
        return false
    }

    fun addPreferencesFromResource(preferencesResId: Int) {
        super.addPreferencesFromResource(preferencesResId)
        // Initialize node preferences
        for (pref in Constants.sBooleanNodePreferenceMap.keySet()) {
            val b: SwitchPreference = findPreference(pref) as SwitchPreference? ?: continue
            val node: String = Constants.sBooleanNodePreferenceMap.get(pref)
            if (FileUtils.isFileReadable(node)) {
                val curNodeValue: String = FileUtils.readOneLine(node)
                b.setChecked(curNodeValue == "1")
                b.setOnPreferenceChangeListener(this)
            } else {
                removePref(b)
            }
        }
        for (pref in Constants.sStringNodePreferenceMap.keySet()) {
            val l: ListPreference = findPreference(pref) as ListPreference? ?: continue
            val node: String = Constants.sStringNodePreferenceMap.get(pref)
            if (FileUtils.isFileReadable(node)) {
                l.setValue(FileUtils.readOneLine(node))
                l.setOnPreferenceChangeListener(this)
            } else {
                removePref(l)
            }
        }
    }

    fun onOptionsItemSelected(item: MenuItem): Boolean {
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