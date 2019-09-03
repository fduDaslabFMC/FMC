package com.android.packageinstaller;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.net.Uri;
import android.os.Binder;
import android.support.annotation.NonNull;
import android.util.Log;

public class FmcContentProvider extends ContentProvider {


    private static final String TAG = "FmcContentProvider";

    private static final boolean DEBUG = true;

    private static final String DATABASE_FILE_NAME = "fmc.db";

    private static final String TABLE_NAME = "fmc_privilege";

    public static final String ID_COLUMN = "id";

    public static final String PKG_NAME_COLUMN = "pkg_name";

    public static final String PERMISSIONS_COLUMN = "permissions";

    public static final String AUTHORITY = "com.hlq07.dfvcmodule.provider";

    private static final Object LOCK = new Object();

    @Override
    public boolean onCreate() {
        fmcDatabase = new FmcDatabaseHelper(getContext(), DATABASE_FILE_NAME, null, 1);
        return true;
    }



    private SQLiteOpenHelper fmcDatabase;

    @Override
    public Uri insert(@NonNull Uri uri, ContentValues values) {
        if (DEBUG) {
            Log.d(TAG, "insert/update a record: uri " + uri + " with values " + values.toString() );
        }

        enforcePermissions(uri);

        SQLiteDatabase db = fmcDatabase.getReadableDatabase();

        long cnt;
        synchronized (LOCK) {
            cnt = db.update(TABLE_NAME, values, PKG_NAME_COLUMN + " = ?",
                    new String[]{values.getAsString(PKG_NAME_COLUMN)}
            );
            if (cnt <= 0) {
                cnt = db.insert(TABLE_NAME, null, values);
            }
        }

        if (DEBUG) {
            Log.d(TAG, "insert/update " + cnt + " item(s).");
        }

        return null;
    }

    @Override
    public Cursor query(@NonNull Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        SQLiteDatabase db = fmcDatabase.getReadableDatabase();
        return db.query(TABLE_NAME,
                projection, selection, selectionArgs, null, null, sortOrder);
    }

    private void enforcePermissions(Uri uri) {
        final int pid = Binder.getCallingPid();
        final int uid = Binder.getCallingUid();
        final int myUid = android.os.Process.myUid();

        if (DEBUG) {
            Log.d(TAG, "myUid=" + myUid + " caller's uid=" + uid);
        }
        if (uid == myUid || uid == 1000 || uid == 0) {
            return;
        }

        throw new SecurityException("Permission Denial: reading "
                + getClass().getName() + " uri " + uri + " from pid=" + pid
                + ", uid=" + uid);
    }




    private static class FmcDatabaseHelper extends SQLiteOpenHelper {

        private static final String CREATE_DATA = String.format(
                "CREATE TABLE %s ("
                        + "%s INTEGER PRIMARY KEY AUTOINCREMENT,"
                        + "%s VARCHAR(255),"
                        + "%s TEXT"
                        + ")"
                , TABLE_NAME, ID_COLUMN, PKG_NAME_COLUMN, PERMISSIONS_COLUMN
        );

        private static final String CREATE_DATA_INDEX =
                "CREATE UNIQUE INDEX pkg_name_idx ON " + TABLE_NAME + " (" + PKG_NAME_COLUMN + ")";

        FmcDatabaseHelper(Context context, String name,
                                 SQLiteDatabase.CursorFactory factory, int version) {
            super(context, name, factory, version);
        }

        @Override
        public void onCreate(SQLiteDatabase db) {
            db.execSQL(CREATE_DATA);
            db.execSQL(CREATE_DATA_INDEX);
        }

        @Override
        public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
            // nothing
        }

    }



    @Override
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        throw new UnsupportedOperationException("Update is not supported.");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Delete is not supported.");
    }

    @Override
    public String getType(Uri uri) {
        throw new UnsupportedOperationException("GetType is not supported.");
    }

}
