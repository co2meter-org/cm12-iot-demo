package com.example.co2meterdemoapplication.ui.main;

import android.content.Context;
import android.net.wifi.aware.WifiAwareManager;

import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentManager;
import androidx.fragment.app.FragmentPagerAdapter;

import com.example.co2meterdemoapplication.R;

/**
 * A [FragmentPagerAdapter] that returns a fragment corresponding to
 * one of the sections/tabs/pages.
 */
public class SectionsPagerAdapter extends FragmentPagerAdapter {

    @StringRes
    private static final int[] TAB_TITLES = new int[]{R.string.tab_text_1, R.string.tab_text_2, R.string.tab_text_3};
    private final Context mContext;

    public SectionsPagerAdapter(Context context, FragmentManager fm) {
        super(fm);
        mContext = context;
    }

    @Override
    public Fragment getItem(int position) {
        // getItem is called to instantiate the fragment for the given page.
        // Return a PlaceholderFragment (defined as a static inner class below).
        if (position == 0) {
            WiFiDemoFragment demoFragment = new WiFiDemoFragment();
            demoFragment.setContext(mContext);
            return demoFragment;
        } else if (position == 1) {
            BLEDemoFragment demoFragment = new BLEDemoFragment();
            demoFragment.mContext = mContext;
            return demoFragment;
        }
        IOTDemoFragment demoFragment = new IOTDemoFragment();
        demoFragment.setContext(mContext);
        return demoFragment;
    }

    @Nullable
    @Override
    public CharSequence getPageTitle(int position) {
        return mContext.getResources().getString(TAB_TITLES[position]);
    }

    @Override
    public int getCount() {
        // Show 2 total pages.
        return 3;
    }
}