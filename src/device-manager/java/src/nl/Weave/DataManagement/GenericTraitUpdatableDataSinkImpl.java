/*

    Copyright (c) 2019 Google, LLC.
    All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

/**
 *    @file
 *      Represents GenericTraitUpdatableDataSink Implementation using native GenericTraitUpdatableDataSink APIs.
 */

package nl.Weave.DataManagement;

import android.os.Build;
import android.util.Log;

import java.math.BigInteger;
import java.util.EnumSet;
import java.util.Random;
import java.util.HashMap;
import java.util.Map;
import java.util.Iterator;

public class GenericTraitUpdatableDataSinkImpl implements GenericTraitUpdatableDataSink
{
    protected GenericTraitUpdatableDataSinkImpl(long traitInstancePtr, WDMClientImpl wdmClient)
    {
        mWDMClient = wdmClient;
        mTraitInstancePtr = traitInstancePtr;
        init(mTraitInstancePtr);
    }

    protected void shutdown()
    {
        if (mWDMClient != null)
        {
            mWDMClient.removeDataSinkRef(mTraitInstancePtr);
            mWDMClient = null;
        }

        if (mTraitInstancePtr != 0)
        {
            shutdown(mTraitInstancePtr);
            mTraitInstancePtr = 0;
        }
        mCompHandler = null;
    }

    public void clear()
    {
        if (mTraitInstancePtr != 0)
        {
            clear(mTraitInstancePtr);
        }
    }

    @Override
    public void setSigned(String path, int value, boolean isConditional)
    {
        boolean isSigned = true;
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setInt(mTraitInstancePtr, path, value, isConditional, isSigned);
    }

    @Override
    public void setSigned(String path, long value, boolean isConditional)
    {
        boolean isSigned = true;
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setInt(mTraitInstancePtr, path, value, isConditional, isSigned);
    }

    @Override
    public void setSigned(String path, BigInteger value, boolean isConditional)
    {
        boolean isSigned = true;
        long convertedVal = value.longValue();
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setInt(mTraitInstancePtr, path, convertedVal, isConditional, isSigned);
    }

    @Override
    public void setUnsigned(String path, int value, boolean isConditional)
    {
        boolean isSigned = false;
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setInt(mTraitInstancePtr, path, value, isConditional, isSigned);
    }

    @Override
    public void setUnsigned(String path, long value, boolean isConditional)
    {
        boolean isSigned = false;
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setInt(mTraitInstancePtr, path, value, isConditional, isSigned);
    }

    @Override
    public void setUnsigned(String path, BigInteger value, boolean isConditional)
    {
        boolean isSigned = false;
        long convertedVal = value.longValue();
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setInt(mTraitInstancePtr, path, convertedVal, isConditional, isSigned);
    }

    @Override
    public void set(String path, double value, boolean isConditional)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setDouble(mTraitInstancePtr, path, value, isConditional);
    }

    @Override
    public void set(String path, boolean value, boolean isConditional)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setBoolean(mTraitInstancePtr, path, value, isConditional);
    }

    @Override
    public void set(String path, String value, boolean isConditional)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setString(mTraitInstancePtr, path, value, isConditional);
    }

    @Override
    public void set(String path, byte[] value, boolean isConditional)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setBytes(mTraitInstancePtr, path, value, isConditional);
    }

    @Override
    public void setNull(String path, boolean isConditional)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        setNull(mTraitInstancePtr, path, isConditional);
    }

    @Override
    public void setSigned(String path, int value)
    {
        setSigned(path, value, false);
    }

    @Override
    public void setSigned(String path, long value)
    {
        setSigned(path, value, false);
    }

    @Override
    public void setSigned(String path, BigInteger value)
    {
        setSigned(path, value, false);
    }

    @Override
    public void setUnsigned(String path, int value)
    {
        setUnsigned(path, value, false);
    }

    @Override
    public void setUnsigned(String path, long value)
    {
        setUnsigned(path, value, false);
    }

    @Override
    public void setUnsigned(String path, BigInteger value)
    {
        setUnsigned(path, value, false);
    }

    @Override
    public void set(String path, double value)
    {
        set(path, value, false);
    }

    @Override
    public void set(String path, boolean value)
    {
        set(path, value, false);
    }

    @Override
    public void set(String path, String value)
    {
        set(path, value, false);
    }

    @Override
    public void set(String path, byte[] value)
    {
        set(path, value, false);
    }

    @Override
    public void setNull(String path)
    {
        setNull(path, false);
    }

    @Override
    public int getInt(String path)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return 0;
        }
        return (int)getInt(mTraitInstancePtr, path);
    }

    @Override
    public long getLong(String path)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return 0;
        }
        return (long)getInt(mTraitInstancePtr, path);
    }

    @Override
    public BigInteger getBigInteger(String path)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return null;
        }

        long value = getInt(mTraitInstancePtr, path);
        BigInteger bigInt = BigInteger.valueOf(value);
        return bigInt;
    }

    @Override
    public double getDouble(String path)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return 0;
        }
        return getDouble(mTraitInstancePtr, path);
    }

    @Override
    public boolean getBoolean(String path)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return false;
        }
        return getBoolean(mTraitInstancePtr, path);
    }

    @Override
    public String getString(String path)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return null;
        }
        return getString(mTraitInstancePtr, path);
    }

    @Override
    public byte[] getBytes(String path)
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return null;
        }
        return getBytes(mTraitInstancePtr, path);
    }

    @Override
    public long getVersion()
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return 0;
        }
        return getVersion(mTraitInstancePtr);
    }

    @Override
    public void beginRefreshData()
    {
        if (mTraitInstancePtr == 0)
        {
            Log.e(TAG, "unexpected err, mTraitInstancePtr is 0");
            return;
        }
        beginRefreshData(mTraitInstancePtr);
    }

    @Override
    public CompletionHandler getCompletionHandler()
    {
        return mCompHandler;
    }

    @Override
    public void setCompletionHandler(CompletionHandler compHandler)
    {
        mCompHandler = compHandler;
    }

    private void onError(Throwable err)
    {
        if (mCompHandler == null)
        {
            Log.e(TAG, "unexpected err, mCompHandler is null");
            return;
        }
        mCompHandler.onError(err);
    }

    private void onRefreshDataComplete()
    {
        if (mCompHandler == null)
        {
            Log.e(TAG, "unexpected err, mCompHandler is null");
            return;
        }
        mCompHandler.onRefreshDataComplete();
    }

    // ----- Protected Members -----

    private CompletionHandler mCompHandler;

    protected void finalize() throws Throwable
    {
        super.finalize();
        clear();
    }

    // ----- Private Members -----

    private long mTraitInstancePtr;
    private WDMClientImpl mWDMClient;
    private final static String TAG = GenericTraitUpdatableDataSink.class.getSimpleName();

    static {
        System.loadLibrary("WeaveDeviceManager");
    }

    private native void init(long genericTraitUpdatableDataSinkPtr);
    private native void shutdown(long genericTraitUpdatableDataSinkPtr);
    private native void clear(long genericTraitUpdatableDataSinkPtr);
    private native void beginRefreshData(long genericTraitUpdatableDataSinkPtr);
    private native void setInt(long genericTraitUpdatableDataSinkPtr, String path, long value, boolean isConditional, boolean isSigned);
    private native void setDouble(long genericTraitUpdatableDataSinkPtr, String path, double value, boolean isConditional);
    private native void setBoolean(long genericTraitUpdatableDataSinkPtr, String path, boolean value, boolean isConditional);
    private native void setString(long genericTraitUpdatableDataSinkPtr, String path, String value, boolean isConditional);
    private native void setNull(long genericTraitUpdatableDataSinkPtr, String path, boolean isConditional);
    private native void setBytes(long genericTraitUpdatableDataSinkPtr, String path, byte[] value, boolean isConditional);
    private native long getInt(long genericTraitUpdatableDataSinkPtr, String path);
    private native double getDouble(long genericTraitUpdatableDataSinkPtr, String path);
    private native boolean getBoolean(long genericTraitUpdatableDataSinkPtr, String path);
    private native String getString(long genericTraitUpdatableDataSinkPtr, String path);
    private native byte[] getBytes(long genericTraitUpdatableDataSinkPtr, String path);
    private native long getVersion(long genericTraitUpdatableDataSinkPtr);
};
