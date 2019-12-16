/*
 *
 *    Copyright (c) 2019 Google LLC
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      Represents Resource Identifier.
 */

package nl.Weave.DataManagement;
import android.os.Build;
import android.util.Log;
import java.math.BigInteger;
import java.util.Objects;

public class ResourceIdentifier
{
    public ResourceIdentifier()
    {
        resourceType = RESOURCE_TYPE_RESERVED;
        resourceId = SELF_NODE_ID;
    }

    private int resourceType;
    private long resourceId;

    public static ResourceIdentifier Make(int resourceType, BigInteger value)
    {
        long resourceId = value.longValue();
        ResourceIdentifier resourceIdentifier = new ResourceIdentifier();
        resourceIdentifier.resourceType = resourceType;
        resourceIdentifier.resourceId = resourceId;
        return resourceIdentifier;
    }

    public static ResourceIdentifier Make(int resourceType, byte[] resourceIdBytes)
    {
        ResourceIdentifier resourceIdentifier = new ResourceIdentifier();
        resourceIdentifier.resourceType = resourceType;

        if (resourceIdBytes.length != 8)
        {
            Log.e(TAG, "unexpected resourceIdentifier, resourceIdBytes should be 8 bytes");
            return null;
        }
        resourceIdentifier.resourceId = byteArrayToLong(resourceIdBytes);

        return resourceIdentifier;
    }

    public static long byteArrayToLong(byte[] value)
    {
        long result = 0;
        for (int i = 0; i < value.length; i++)
        {
           result += ((long) value[i] & 0xffL) << (8 * i);
        }
        return result;
    }

    public static byte[] LongToByteArray(long value) {
        byte[] bytes = new byte[8];
        for (int i = 0; i < 8; i++) {
            bytes[i] = (byte)(value >>> (i * 8));
        }
        return bytes;
    }

    @Override
    public boolean equals(Object other) {
        if (other == this) {
            return true;
        }
        else if (other instanceof ResourceIdentifier) {
            ResourceIdentifier otherId = (ResourceIdentifier) other;
            return this.resourceType == otherId.resourceType && this.resourceId == otherId.resourceId;
        }
        else
        {
            return false;
        }
    }

    @Override
    public int hashCode() {
        return Objects.hash(resourceId, resourceType);
    }

    public static final int RESOURCE_TYPE_RESERVED = 0;
    public static final long SELF_NODE_ID = -2;

    private final static String TAG = ResourceIdentifier.class.getSimpleName();
}
