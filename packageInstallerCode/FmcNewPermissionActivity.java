package com.android.packageinstaller;

import android.content.ContentValues;
import android.net.Uri;
import android.support.annotation.NonNull;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.ProgressBar;
import android.widget.TextView;


public class FmcNewPermissionActivity extends AppCompatActivity implements View.OnClickListener {

    private static final String TAG = "FmcNewPermissionAct";

    private static final boolean DEBUG = false;

    // private static final String URL = "http://baidu.com?pkgname=";

    private void queryRecommendation(String pkgName) {

        // OkHttpClient client = new OkHttpClient();
        // Request request = new Request.Builder()
        //         .url(URL + pkgName)
        //         .build();
        // client.newCall(request).enqueue(new Callback() {
        //     @Override
        //     public void onFailure(Call call, IOException e) {
        //         runOnUiThread(() -> enableAll(recommendationString) );
        //     }
        //     @Override
        //     public void onResponse(Call call, Response response) throws IOException {
        //         recommendationString = response.body().string();
        //         runOnUiThread(() -> enableAll(recommendationString) );
        //    }
        // });

        enableAll(recommendationString);
    }




    private CheckBox[] permissionChecker;

    private Button confirmBtn;

    private TextView pkgNameTextView;

    private ProgressBar bar;

    private static final String DEFAULT_PRIVILEGE = "000";

    private volatile String recommendationString = DEFAULT_PRIVILEGE;

    private volatile String pkgName = "Unknown package name";


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.fmc_new_permission);


        if (getIntent().hasExtra("pkg_name") ) {
            pkgName = getIntent().getStringExtra("pkg_name");
        }

        // checkbox
        permissionChecker = new CheckBox[3];
        permissionChecker[0] = findViewById(R.id.confirm_firstPermission);
        permissionChecker[1] = findViewById(R.id.confirm_secondPermission);
        permissionChecker[2] = findViewById(R.id.confirm_thirdPermission);

        // btn
        confirmBtn = findViewById(R.id.confirm_btn);
        confirmBtn.setOnClickListener(this);

        // textView
        pkgNameTextView = findViewById(R.id.confirm_pkgName);
        pkgNameTextView.setText(pkgName);

        // progress bar
        bar = findViewById(R.id.confirm_processBar);

        unableAll();

        queryRecommendation(pkgName);
    }




    @Override
    public void onClick(View v) {
        switch (v.getId() ) {
            case R.id.confirm_btn: {
                saveConfirmResult(pkgName);
                finish();
            }
            default:
                // nothing to do
        }
    }

    @Override
    public void onBackPressed() {
        super.onBackPressed();
        saveConfirmResult(pkgName, recommendationString);
    }




    private void unableAll() {
        for (CheckBox checker: permissionChecker) {
            checker.setEnabled(false);
        }
        confirmBtn.setEnabled(false);
        bar.setVisibility(View.VISIBLE);
        setResult(RESULT_CANCELED);
    }

    private void enableAll(int flags) {
        for (int i = 0; i < permissionChecker.length; i++) {
            CheckBox checker = permissionChecker[i];
            checker.setEnabled(true);
            checker.setChecked(((flags >> i) & 1) == 1);
        }

        confirmBtn.setEnabled(true);
        bar.setVisibility(View.INVISIBLE);
        setResult(RESULT_OK);
    }

    private void enableAll(String str) {
        str = adaptPermissionString(str);

        int flags = 0;
        for (int i = 0; i < 3; i++) {
            if (str.charAt(i) == '1') {
                flags |= 1 << i;
            }
        }
        enableAll(flags);
    }


    private void saveConfirmResult(@NonNull String pkgName) {
        StringBuilder res = new StringBuilder();
        for (int i = 0; i < 3; i++) {
            boolean o = permissionChecker[i].isChecked();
            res.append(o ? '1' : '0');
        }
        saveConfirmResult(pkgName, res.toString() );
    }

    private void saveConfirmResult(@NonNull String pkgName, String permissions) {
        permissions = adaptPermissionString(permissions);

        Uri uri = Uri.parse("content://" + FmcContentProvider.AUTHORITY);

        ContentValues values = new ContentValues();
        values.put(FmcContentProvider.PKG_NAME_COLUMN, pkgName);
        values.put(FmcContentProvider.PERMISSIONS_COLUMN, permissions);

        getContentResolver().insert(uri, values);
    }


    private static String adaptPermissionString(String permissions) {
        if (TextUtils.isEmpty(permissions) ) {
            return DEFAULT_PRIVILEGE;
        }

        if (permissions.length() < 3) {
            permissions = permissions + DEFAULT_PRIVILEGE;
        }

        // replace all invalid characters with '0'
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < 3; i++) {
            result.append(permissions.charAt(i) == '1' ? '1' : '0');
        }
        return result.toString();
    }

}
