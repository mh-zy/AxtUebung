package com.axt.quiz;

import android.app.ComponentCaller;
import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * 唯一的一小块 Java，干两件事：
 *
 * 1) 给系统输入法一个「真正的编辑器」。
 *    NativeActivity 的窗口不是文本编辑器（onCheckIsTextEditor() 为 false、inputType 是 TYPE_NULL），
 *    输入法不会给这种窗口弹键盘，所以挂一个 2x2、几乎全透明的 EditText：需要键盘时让它拿到焦点并
 *    请求输入法，输入法提交的文字（含中文整段提交）通过 JNI 回传给 native，退格 / 回车也一并转发。
 *    注意：requestFocus()/showSoftInput() 是 View / 输入法 API，必须在 UI 线程上调，
 *    否则抛异常（以前从渲染线程调、异常被 ExceptionClear 吞掉，看着像「什么都没发生」）。
 *
 * 2) SAF（系统文档选择器）桥。
 *    Android 打开 / 保存文件都要走「另一个 Activity + 结果回调」，纯 C++ 侧既打不开
 *    content:// 也拿不到回调，所以由 Java 负责：
 *      * 打开：startPickDocuments 弹对话框，takePickedDocumentPaths 把选中的文件复制到应用目录
 *      * 保存：startPickSaveTarget 弹对话框并记下目标文档，takePickedSaveTarget 给出一个应用内的
 *              临时文件路径给 native 写，native 写完再 commitSaveTarget 复制进目标文档
 *
 *    这里**故意做成异步的两步**（native 每帧用 pollPickerState 轮询），不要在 Java 里
 *    「开对话框 + 阻塞等结果」：系统在对话框盖上来时会停掉本应用，而 native_app_glue 的
 *    onPause/onStop 会阻塞主线程直到渲染线程处理完命令 —— 渲染线程若卡在这一帧里等结果，
 *    主线程就卡在 onPause 里，实测 5 秒即 ANR。
 */
public class MainActivity extends NativeActivity {
    /**
     * 自己再 load 一次 native 库。
     *
     * 为什么必须再 load：清单里的 android.app.lib_name 是**系统**的 NativeActivity（引导 class loader）
     * 去 System.loadLibrary 的，那份登记挂在引导 class loader 上；
     * 而 nativeOnText / nativeOnKey 是声明在**这个类**（应用 class loader）里的 → 解析不到，
     * 一敲字就崩：java.lang.UnsatisfiedLinkError: No implementation found for ... nativeOnText。
     * 同一个进程里 dlopen 同一个 .so 没问题（系统按引用计数复用），这里补一次就够。
     */
    static {
        System.loadLibrary("axtquiz");
    }

    private static final String TAG = "AxtQuiz";

    /** 等对话框结果的上限：这么久还没动静就按「取消」处理（用户可能把它丢在后台不管了） */
    private static final long PICK_TIMEOUT_MS = 10L * 60L * 1000L;

    // ---- 系统对话框的状态（native 侧按这些值判断）----
    /** 没有正在进行的对话框 */
    private static final int PICK_STATE_NONE = 0;
    /** 对话框还在（用户正在挑） */
    private static final int PICK_STATE_WAITING = 1;
    /** 结果已经回来了，等 native 来取 */
    private static final int PICK_STATE_READY = 2;

    /** 一次 SAF 调用的状态（同一时刻只会有一次对话框） */
    private final Object pickerLock = new Object();
    private boolean pickerActive = false;      // 有对话框在飞行中
    private boolean pickerDone = false;        // 结果已经回来了
    private long pickerStartedAt = 0;          // 这次对话框什么时候弹出来的（超时兜底用）
    private int pickerRequest = 0;
    private int pickerSerial = 0x5A00;         // 每次都用新的 requestCode：迟到的旧回调会被忽略
    private int pickerResultCode = 0;
    private Intent pickerData = null;
    /** ACTION_CREATE_DOCUMENT 选中的目标文档：等 native 写好内容再往里写 */
    private Uri pendingSaveUri = null;

    private EditText imeView;
    private OnBackInvokedCallback backCallback;
    private static final long BACK_DEBOUNCE_MS = 450L;
    private long lastBackRequestAt = 0L;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        imeView = new EditText(this);
        imeView.setAlpha(0.01f);                // 用户看不见（但仍然是 VISIBLE，可获焦点）
        imeView.setBackgroundColor(0x00000000);
        imeView.setTextColor(0x00000000);
        imeView.setCursorVisible(false);
        imeView.setFocusable(true);
        imeView.setFocusableInTouchMode(true);
        imeView.setSingleLine(true);
        addContentView(imeView, new ViewGroup.LayoutParams(2, 2));
        imeView.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(Editable s) {
                if (s.length() > 0) {
                    nativeOnText(s.toString());    // 中文输入法「整段提交」也走这条
                    s.clear();
                }
            }
        });
        imeView.setOnKeyListener((v, keyCode, event) -> {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (keyCode == KeyEvent.KEYCODE_DEL) {
                    nativeOnKey(8);                // 退格：输入框为空时不会有文字变化
                    return true;
                }
                if (keyCode == KeyEvent.KEYCODE_ENTER) {
                    nativeOnKey(13);
                    return true;
                }
            }
            return false;
        });

        // Android 13+ 的返回手势走 OnBackInvokedDispatcher；如果不拦截，
        // NativeActivity 会直接 finish，C++ 页面状态根本没有机会处理。
        if (Build.VERSION.SDK_INT >= 33) {
            backCallback = this::requestNativeBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        }
    }

    @Override
    public void onBackPressed() {
        requestNativeBack();
    }

    @Override
    protected void onDestroy() {
        if (Build.VERSION.SDK_INT >= 33 && backCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        super.onDestroy();
    }

    private void requestNativeBack() {
        // 系统手势可能在一帧里回调多次；同一手势只允许进入 native 一次。
        final long now = SystemClock.elapsedRealtime();
        if (lastBackRequestAt != 0L && now - lastBackRequestAt < BACK_DEBOUNCE_MS) {
            return;
        }
        lastBackRequestAt = now;
        nativeRequestBack();
    }

    /** native 调用（渲染线程）→ 转到 UI 线程弹出软键盘 */
    public void showKeyboard() {
        runOnUiThread(() -> {
            imeView.bringToFront();
            boolean focused = imeView.requestFocus();
            if (!focused) {
                focused = imeView.requestFocusFromTouch();
            }
            InputMethodManager imm =
                    (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm == null) {
                Log.i(TAG, "没有 InputMethodManager");
                return;
            }
            imm.restartInput(imeView);
            boolean requested = imm.showSoftInput(imeView, InputMethodManager.SHOW_FORCED);
            Log.i(TAG, "弹出键盘：requestFocus=" + focused + " showSoftInput=" + requested
                    + " isActive=" + imm.isActive(imeView));
        });
    }

    /** native 调用（渲染线程）→ 转到 UI 线程收起软键盘 */
    public void hideKeyboard() {
        runOnUiThread(() -> {
            InputMethodManager imm =
                    (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.hideSoftInputFromWindow(imeView.getWindowToken(), 0);
            }
            imeView.clearFocus();
        });
    }

    // =======================================================================
    // SAF：弹出对话框（都立即返回，结果之后由 native 轮询取走）
    // =======================================================================

    /** native 调用（渲染线程）：弹「打开文件」对话框；返回 false = 没弹起来（日志里有原因） */
    public boolean startPickDocuments(boolean multiple) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // 题库就是纯文本：用 */* 保证 .quiz / .txt / 用户自己改过名的文件都能看见
        // （坏文件由 native 的解析器拒绝，不会进题库列表）
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, multiple);
        return startPicker(intent, "打开");
    }

    /** native 调用（渲染线程）：弹「保存文件」对话框；返回 false = 没弹起来 */
    public boolean startPickSaveTarget(String suggestedName, String mimeType) {
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mimeType);
        intent.putExtra(Intent.EXTRA_TITLE, suggestedName);
        return startPicker(intent, "保存");
    }

    /**
     * native 调用（渲染线程，每帧）：对话框有结果了吗？
     * @return 0 = 没有对话框；1 = 还在等；2 = 结果到了（可以 take 了）
     */
    public int pollPickerState() {
        synchronized (pickerLock) {
            if (!pickerActive) {
                return PICK_STATE_NONE;
            }
            if (!pickerDone && System.currentTimeMillis() - pickerStartedAt > PICK_TIMEOUT_MS) {
                // 兜底：用户把对话框丢在后台不管、或系统没回调 → 按取消放行，
                // 免得界面一直卡在「等对话框」（那期间导入/导出的按钮点了都没反应）
                Log.w(TAG, "系统对话框超过 " + (PICK_TIMEOUT_MS / 60000L) + " 分钟没有结果，按取消处理");
                pickerActive = false;
                pickerDone = false;
                return PICK_STATE_NONE;
            }
            return pickerDone ? PICK_STATE_READY : PICK_STATE_WAITING;
        }
    }

    // =======================================================================
    // SAF：取结果
    // =======================================================================

    /**
     * native 调用（渲染线程）：「打开文件」的结果 —— 把用户选中的文件复制到 cacheDir。
     * @return 本地文件绝对路径；用户取消 = null；一个都没复制成功 = 空数组
     */
    public String[] takePickedDocumentPaths(String cacheDir) {
        try {
            final Intent data;
            final int resultCode;
            synchronized (pickerLock) {
                data = pickerData;
                resultCode = pickerResultCode;
            }
            if (resultCode != RESULT_OK || data == null) {
                Log.i(TAG, "打开文件：用户取消");
                return null;
            }
            List<Uri> uris = new ArrayList<>();
            if (data.getClipData() != null) {
                int count = data.getClipData().getItemCount();
                for (int i = 0; i < count; ++i) {
                    uris.add(data.getClipData().getItemAt(i).getUri());
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
            if (uris.isEmpty()) {
                Log.w(TAG, "打开文件：结果里没有 URI");
                return new String[0];
            }
            File dir = new File(cacheDir);
            deleteContents(dir);
            if (!dir.isDirectory() && !dir.mkdirs()) {
                Log.e(TAG, "打开文件：建不了缓存目录 " + cacheDir);
                return new String[0];
            }
            List<String> paths = new ArrayList<>();
            for (int i = 0; i < uris.size(); ++i) {
                Uri uri = uris.get(i);
                File target = new File(dir, uniqueName(dir, safeFileName(displayNameOf(uri), i)));
                if (!copyToFile(uri, target)) {
                    continue;   // 单个失败不影响其它文件
                }
                paths.add(target.getAbsolutePath());
            }
            if (paths.isEmpty()) {
                Log.e(TAG, "打开文件：选中的文件一个都没能复制出来（读权限被拒？）");
                return new String[0];
            }
            Log.i(TAG, "打开文件：已复制 " + paths.size() + " 个文件到 " + cacheDir);
            return paths.toArray(new String[0]);
        } finally {
            finishPick();
        }
    }

    /**
     * native 调用（渲染线程）：「保存文件」的结果 —— 返回 native 该往哪个本地临时文件写内容。
     * @return 临时文件绝对路径；用户取消 = null；拿不到位置 = 空串
     *
     * SAF 是「先选位置、后写内容」：这里只能拿到目标文档的 URI，还不能写，所以
     * 先记在 pendingSaveUri 里，让 native 写到临时文件，写完再调 commitSaveTarget。
     */
    public String takePickedSaveTarget(String scratchDir) {
        try {
            final Intent data;
            final int resultCode;
            synchronized (pickerLock) {
                data = pickerData;
                resultCode = pickerResultCode;
            }
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                Log.i(TAG, "保存文件：用户取消");
                return null;
            }
            File dir = new File(scratchDir);
            if (!dir.isDirectory() && !dir.mkdirs()) {
                Log.e(TAG, "保存文件：建不了临时目录 " + scratchDir);
                return "";
            }
            File scratch = new File(dir, "pending-export.quiz");
            pendingSaveUri = data.getData();
            Log.i(TAG, "保存文件：目标文档 = " + pendingSaveUri + "，临时文件 = " + scratch);
            return scratch.getAbsolutePath();
        } finally {
            finishPick();
        }
    }

    /**
     * native 调用（渲染线程）：把 localPath 的内容写进刚才选中的文档。
     * 成功返回给用户看的位置（例如 Download/xxx.quiz），失败返回 null。
     * （native 写完就调，和 Activity 生命周期无关，不会卡住主线程。）
     */
    public String commitSaveTarget(String localPath) {
        Uri uri = pendingSaveUri;
        pendingSaveUri = null;
        if (uri == null) {
            Log.e(TAG, "保存文件：还没选位置就提交了内容");
            return null;
        }
        FileInputStream in = null;
        OutputStream out = null;
        try {
            in = new FileInputStream(localPath);
            out = getContentResolver().openOutputStream(uri, "wt");   // wt：截断后写
            if (out == null) {
                Log.e(TAG, "保存文件：拿不到输出流 " + uri);
                return null;
            }
            byte[] buffer = new byte[64 * 1024];
            long total = 0;
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
                total += read;
            }
            out.flush();
            Log.i(TAG, "保存文件：已写入 " + total + " 字节到 " + uri);
        } catch (Exception e) {
            Log.e(TAG, "保存文件：写入用户选中的文档失败 " + uri, e);
            return null;
        } finally {
            closeQuietly(in);
            closeQuietly(out);
            if (!new File(localPath).delete()) {
                Log.w(TAG, "保存文件：临时文件没删掉 " + localPath);
            }
        }
        String display = describeUri(uri);
        Log.i(TAG, "保存文件：完成，位置 = " + display);
        return display;
    }

    // =======================================================================
    // 对话框的公共部分
    // =======================================================================

    /** 在 UI 线程弹出对话框（立即返回；结果由 onActivityResult 记下来） */
    private boolean startPicker(Intent intent, String what) {
        final int requestCode;
        synchronized (pickerLock) {
            if (pickerActive) {
                Log.w(TAG, "已经有一个系统对话框在进行中，忽略这次请求");
                return false;
            }
            requestCode = ++pickerSerial;   // 每次都不一样的 requestCode，迟到的旧回调会被忽略
            pickerActive = true;
            pickerDone = false;
            pickerStartedAt = System.currentTimeMillis();
            pickerRequest = requestCode;
            pickerResultCode = 0;
            pickerData = null;
        }
        runOnUiThread(() -> {
            try {
                Log.i(TAG, "弹出系统对话框（" + what + "，requestCode=" + requestCode + "）");
                startActivityForResult(intent, requestCode);
            } catch (Exception e) {
                Log.e(TAG, "弹不出系统对话框", e);
                synchronized (pickerLock) {
                    pickerActive = false;
                    pickerDone = false;
                }
            }
        });
        return true;
    }

    /** 结果取走了：清掉状态，可以开始下一次 */
    private void finishPick() {
        synchronized (pickerLock) {
            pickerActive = false;
            pickerDone = false;
            pickerRequest = 0;
            pickerResultCode = 0;
            pickerData = null;
        }
    }

    /** 收到选择器结果（UI 线程）：记下来等 native 来取（重复 / 迟到的回调直接忽略） */
    private void deliverPickerResult(int requestCode, int resultCode, Intent data) {
        synchronized (pickerLock) {
            if (!pickerActive || pickerDone || requestCode != pickerRequest) {
                return;
            }
            pickerResultCode = resultCode;
            pickerData = data;
            pickerDone = true;
            Log.i(TAG, "系统对话框返回：requestCode=" + requestCode + " resultCode=" + resultCode);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        deliverPickerResult(requestCode, resultCode, data);
    }

    // 新系统（API 34+）走这个重载，默认实现会转回上面那个三参数版本；
    // 两个都覆盖 + deliverPickerResult 内部去重，老系统新系统都能收到结果。
    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data, ComponentCaller caller) {
        super.onActivityResult(requestCode, resultCode, data, caller);
        deliverPickerResult(requestCode, resultCode, data);
    }

    /** content:// 的显示名（例如 xxx.quiz） */
    private String displayNameOf(Uri uri) {
        if (uri == null) {
            return null;
        }
        ContentResolver resolver = getContentResolver();
        Cursor cursor = null;
        try {
            cursor = resolver.query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    return cursor.getString(index);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "查不到文件名 " + uri, e);
        } finally {
            closeQuietly(cursor);
        }
        return uri.getLastPathSegment();
    }

    /** 给用户看的位置：能拿到文档路径就用（Download/xxx.quiz），否则退回文件名 */
    private String describeUri(Uri uri) {
        try {
            String documentId = DocumentsContract.getDocumentId(uri);
            if (documentId != null && !documentId.isEmpty()) {
                int colon = documentId.indexOf(':');
                return colon >= 0 ? documentId.substring(colon + 1) : documentId;
            }
        } catch (Exception ignored) {
            // 不是 DocumentsProvider 的 URI：下面退回文件名
        }
        String name = displayNameOf(uri);
        return (name == null || name.isEmpty()) ? uri.toString() : name;
    }

    /** 去掉路径分隔符等不能做文件名的字符；名字为空时给个兜底 */
    private static String safeFileName(String name, int index) {
        String clean = (name == null || name.trim().isEmpty()) ? "" : name.trim();
        clean = clean.replace('/', '_').replace('\\', '_').replace('\0', '_');
        if (clean.isEmpty()) {
            clean = "bank-" + index + ".quiz";
        }
        if (clean.charAt(0) == '.') {
            clean = "f" + clean;   // 别生成隐藏文件
        }
        return clean;
    }

    /** 同一个目录里重名时加 -1 / -2 后缀（一次选多个同名文件的情况） */
    private static String uniqueName(File dir, String name) {
        File candidate = new File(dir, name);
        if (!candidate.exists()) {
            return name;
        }
        int suffix = 1;
        while (true) {
            File next = new File(dir, name + "-" + suffix);
            if (!next.exists()) {
                return next.getName();
            }
            ++suffix;
        }
    }

    private boolean copyToFile(Uri uri, File target) {
        InputStream in = null;
        FileOutputStream out = null;
        try {
            in = getContentResolver().openInputStream(uri);
            if (in == null) {
                Log.e(TAG, "打开文件：读不了 " + uri);
                return false;
            }
            out = new FileOutputStream(target);
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            out.flush();
            return true;
        } catch (Exception e) {
            Log.e(TAG, "打开文件：复制失败 " + uri, e);
            if (!target.delete()) {
                Log.w(TAG, "打开文件：删不掉半截文件 " + target);
            }
            return false;
        } finally {
            closeQuietly(in);
            closeQuietly(out);
        }
    }

    private void deleteContents(File dir) {
        File[] children = dir.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            if (!child.delete()) {
                Log.w(TAG, "删不掉上次的缓存文件 " + child);
            }
        }
    }

    private static void closeQuietly(Closeable closeable) {
        if (closeable == null) {
            return;
        }
        try {
            closeable.close();
        } catch (IOException ignored) {
            // 关闭失败没什么可做的
        }
    }

    /**
     * 写系统剪贴板（设置页「提示词」的复制按钮用；native 侧 platform::setClipboardText 调它）。
     * @return true = 写成功
     */
    public boolean setClipboard(String text) {
        try {
            ClipboardManager manager =
                    (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (manager == null) {
                return false;
            }
            manager.setPrimaryClip(ClipData.newPlainText("AxtQuiz", text == null ? "" : text));
            return true;
        } catch (Exception error) {
            Log.w(TAG, "写剪贴板失败", error);
            return false;
        }
    }

    private static native void nativeOnText(String text);
    private static native void nativeOnKey(int keyCode);
    private static native void nativeRequestBack();
}
