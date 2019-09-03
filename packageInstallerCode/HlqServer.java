package com.android.server.hlqserver;

import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Slog;
import android.content.Context;


public class HlqServer extends Binder {

    private static final String TAG = "HlqServer";

    private static final boolean DEBUG = true;


    public static final String TOKEN = "hlq_server";

    public static final int GET_FLAGS_CODE = 1;

    public static final int SET_FLAGS_CODE = 2;

    public static final int QUERY_FMC_PERMISSION_CODE = 3;

    public static final int INSERT_FMC_PERMISSION_CODE = 4;

    private static final String PKG_NAME_COLUMN = "pkg_name";

    private static final String PERMISSIONS_COLUMN = "permissions";

    private static final String AUTHORITY = "com.hlq07.dfvcmodule.provider";



    public HlqServer(Context context) {
        this.mContext = context;
    }

    private Context mContext;

    private volatile int flags;

    private int getFlags() {
        if (DEBUG) {
            Slog.d(TAG, "get value of flags: " + flags);
        }
        return flags;
    }

    private void setFlags(int flags) {
        if (DEBUG) {
            Slog.d(TAG, "set flags with " + flags);
        }

        this.flags = flags;
    }

    private int queryFmcPermission(String pkgName) {
        final Uri uri = Uri.parse("content://" + AUTHORITY);
        final String[] projection = {
                PKG_NAME_COLUMN,
                PERMISSIONS_COLUMN
        };
        final String selection = PKG_NAME_COLUMN + "=?";
        final String[] selectionArgs = {
                pkgName
        };
        Cursor cursor = mContext.getContentResolver().query(uri, projection, selection, selectionArgs, null);

        int permissions = -1;
        if (cursor != null) {
            if (cursor.moveToNext()) {
                int permissionIndex =
                        cursor.getColumnIndex(PERMISSIONS_COLUMN);
                String perStr = cursor.getString(permissionIndex);
                if (perStr.length() < 3) {
                    perStr += "000";
                }

                permissions = 0;
                for (int i = 0; i < 3; i++) {
                    if (perStr.charAt(i) == '1') {
                        permissions |= 1 << i;
                    }
                }
            }
            cursor.close();
        }

        return permissions;
    }



    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
        switch(code) {
            case GET_FLAGS_CODE: {
                data.enforceInterface(TOKEN);
                reply.writeInt(getFlags() );
                return true;
            }
            case SET_FLAGS_CODE: {
                data.enforceInterface(TOKEN);
                setFlags(data.readInt() );
                reply.writeInt(getFlags() );
                return true;
            }
            case QUERY_FMC_PERMISSION_CODE: {
                data.enforceInterface(TOKEN);
                String pkgName = data.readString();
                int permissions = queryFmcPermission(pkgName);
                reply.writeInt(permissions);
                return true;
            }
            case INSERT_FMC_PERMISSION_CODE: {
                data.enforceInterface(TOKEN);
                throw new UnsupportedOperationException("Insert fmc permissions is not supported.");
            }
            default:
                return false;
        }
    }

}
