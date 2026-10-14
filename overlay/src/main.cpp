#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <filesystem>
#include "metrics.hpp"
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"shell32.lib")

using namespace Gdiplus;
namespace fs=std::filesystem;
constexpr UINT TRAY=WM_APP+10;
constexpr UINT CMD_EXIT=100,CMD_HIDE=101,CMD_RETRY=102,CMD_INFO=103,CMD_SELECT=104;
constexpr UINT PICKER=201,REFRESH=202,START_CAPTURE=203;
HWND window=nullptr;HINSTANCE instance=nullptr;fs::path base;
HWND controller=nullptr,processPicker=nullptr,captureStatus=nullptr;
bool visible=true,darkText=false,diagnostics=false;int duration=0;ULONGLONG began=0;
std::wstring processName,mainText=L"-- FPS",subText=L"请选择进程";
DWORD selectedPid=0;ULONGLONG selectedBirth=0;
DWORD target=0,blocked=0;HWND targetWindow=nullptr;int screenX=0,screenY=0;
NOTIFYICONDATAW tray{};

std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);std::wstring w(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),w.data(),n);return w;}
std::string utf8(const std::wstring& w){int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);std::string s(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),s.data(),n,nullptr,nullptr);return s;}
std::string escape(const std::string& s){std::string out;for(char c:s){if(c=='"'||c=='\\')out+='\\';if(c=='\n'){out+="\\n";continue;}if(c=='\r')continue;if((unsigned char)c<32)continue;out+=c;}return out;}
void errorBox(const std::wstring& s){MessageBoxW(nullptr,s.c_str(),L"FPS 悬浮窗",MB_OK|MB_ICONINFORMATION);}

struct Discovery {DWORD pid=0;HWND hwnd=nullptr;long long area=0;};
struct ProcessChoice {DWORD pid=0;ULONGLONG birth=0;std::wstring name,title;};
std::vector<ProcessChoice> choices;
ULONGLONG processBirth(DWORD pid){HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return 0;FILETIME created{},exited{},kernel{},user{};BOOL ok=GetProcessTimes(p,&created,&exited,&kernel,&user);CloseHandle(p);return ok?((ULONGLONG)created.dwHighDateTime<<32)|created.dwLowDateTime:0;}
BOOL CALLBACK findWindow(HWND hwnd,LPARAM param){auto* d=(Discovery*)param;DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);if(pid!=d->pid||!IsWindowVisible(hwnd)||GetWindow(hwnd,GW_OWNER))return TRUE;RECT r{};GetClientRect(hwnd,&r);long long a=(long long)(r.right-r.left)*(r.bottom-r.top);if(a>d->area){d->area=a;d->hwnd=hwnd;}return TRUE;}
Discovery discover(){
    Discovery result;if(!selectedPid||!selectedBirth||processBirth(selectedPid)!=selectedBirth)return result;
    result.pid=selectedPid;EnumWindows(findWindow,(LPARAM)&result);return result;
}
void refreshProcesses(){
    DWORD previous=0;auto index=SendMessageW(processPicker,CB_GETCURSEL,0,0);if(index>0&&(size_t)index<=choices.size())previous=choices[(size_t)index-1].pid;
    choices.clear();HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot!=INVALID_HANDLE_VALUE){PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);if(Process32FirstW(snapshot,&entry))do{
        if(entry.th32ProcessID==0||entry.th32ProcessID==GetCurrentProcessId()||_wcsicmp(entry.szExeFile,L"PresentMon.exe")==0)continue;
        ProcessChoice choice;choice.pid=entry.th32ProcessID;choice.name=entry.szExeFile;choice.birth=processBirth(choice.pid);
        Discovery d;d.pid=choice.pid;EnumWindows(findWindow,(LPARAM)&d);if(d.hwnd){wchar_t title[512]{};GetWindowTextW(d.hwnd,title,512);choice.title=title;}choices.push_back(std::move(choice));
    }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);}
    std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){int compare=_wcsicmp(a.name.c_str(),b.name.c_str());return compare?compare<0:a.pid<b.pid;});
    SendMessageW(processPicker,CB_RESETCONTENT,0,0);SendMessageW(processPicker,CB_ADDSTRING,0,(LPARAM)L"请选择正在运行的图形进程");int selected=0;
    for(size_t i=0;i<choices.size();i++){const auto& c=choices[i];std::wstring label=c.name+L"  [PID "+std::to_wstring(c.pid)+L"]";if(!c.title.empty())label+=L"  — "+c.title;if(!c.birth)label+=L"  [权限不可读取]";SendMessageW(processPicker,CB_ADDSTRING,0,(LPARAM)label.c_str());if(c.pid==previous)selected=(int)i+1;}
    SendMessageW(processPicker,CB_SETCURSEL,selected,0);
}

class Collector {
    HANDLE process=nullptr,pipe=nullptr,job=nullptr;std::thread reader;std::wstring session;
    std::atomic<bool> stopping{false};std::mutex mutex;fps::Metrics metrics;std::string problem;
    DWORD pid=0;
public:
    ~Collector(){stop();}
    bool start(DWORD id){
        stop();pid=id;{std::lock_guard guard(mutex);metrics={};problem.clear();}stopping=false;
        auto exe=base/L"PresentMon.exe";if(!fs::exists(exe)){std::lock_guard guard(mutex);problem="PresentMon.exe missing";return false;}
        session=L"REA_FPSOverlay_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(id)+L"_"+std::to_wstring(GetTickCount64());
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE write=nullptr;
        if(!CreatePipe(&pipe,&write,&sa,0)){std::lock_guard guard(mutex);problem="CreatePipe failed";return false;}SetHandleInformation(pipe,HANDLE_FLAG_INHERIT,0);
        HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=input;si.hStdOutput=write;si.hStdError=write;PROCESS_INFORMATION pi{};
        std::wstring cmd=L"\""+exe.wstring()+L"\" --process_id "+std::to_wstring(id)+L" --output_stdout --no_console_stats --no_track_gpu --no_track_display --no_track_input --terminate_on_proc_exit --session_name "+session;
        BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,base.c_str(),&si,&pi);CloseHandle(write);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
        if(!ok){CloseHandle(pipe);pipe=nullptr;std::lock_guard guard(mutex);problem="PresentMon start failed: "+std::to_string(GetLastError());return false;}
        job=CreateJobObjectW(nullptr,nullptr);if(job){JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits));AssignProcessToJobObject(job,pi.hProcess);}
        process=pi.hProcess;ResumeThread(pi.hThread);CloseHandle(pi.hThread);
        reader=std::thread([this]{char buffer[8192];DWORD n=0;std::string pending;while(!stopping&&ReadFile(pipe,buffer,sizeof(buffer),&n,nullptr)&&n){pending.append(buffer,n);size_t pos;while((pos=pending.find('\n'))!=std::string::npos){std::string line=pending.substr(0,pos);pending.erase(0,pos+1);std::lock_guard guard(mutex);metrics.line(line,pid,GetTickCount64());std::string lower=line;for(char& c:lower)c=(char)tolower((unsigned char)c);if(lower.find("error")!=std::string::npos||lower.find("failed")!=std::string::npos||lower.find("administrator")!=std::string::npos||lower.find("access is denied")!=std::string::npos)problem=line.substr(0,600);}if(pending.size()>65536){std::lock_guard guard(mutex);problem="Unexpected collector output";break;}}});
        return true;
    }
    void stop(){
        stopping=true;
        if(process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT){
            // Stop only our named ETW session. Never terminate the game or another collector.
            auto exe=base/L"PresentMon.exe";std::wstring cmd=L"\""+exe.wstring()+L"\" --terminate_existing_session --session_name "+session;
            STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
            if(CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,base.c_str(),&si,&pi)){if(WaitForSingleObject(pi.hProcess,2000)==WAIT_TIMEOUT)TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
            if(WaitForSingleObject(process,2000)==WAIT_TIMEOUT)TerminateProcess(process,1);
        }
        if(reader.joinable()){CancelSynchronousIo((HANDLE)reader.native_handle());reader.join();}
        if(process)CloseHandle(process);if(pipe)CloseHandle(pipe);if(job)CloseHandle(job);process=pipe=job=nullptr;pid=0;
    }
    bool alive()const{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
    fps::Reading reading(){std::lock_guard guard(mutex);return metrics.current(GetTickCount64());}
    std::string error(){std::lock_guard guard(mutex);return problem;}
    uint64_t count(){std::lock_guard guard(mutex);return metrics.rows;}
} collector;

void roundRect(Graphics& g,const RectF& r,REAL radius,const Color& color){GraphicsPath p;p.AddArc(r.X,r.Y,radius*2,radius*2,180,90);p.AddArc(r.GetRight()-radius*2,r.Y,radius*2,radius*2,270,90);p.AddArc(r.GetRight()-radius*2,r.GetBottom()-radius*2,radius*2,radius*2,0,90);p.AddArc(r.X,r.GetBottom()-radius*2,radius*2,radius*2,90,90);p.CloseFigure();SolidBrush brush(color);g.FillPath(&brush,&p);}
void content(Graphics& g,REAL scale,const std::wstring& number,const std::wstring& label){
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.ScaleTransform(scale,scale);
    if(!darkText)roundRect(g,RectF(0,0,200,76),12,Color(220,19,23,31));
    FontFamily face(L"Segoe UI");Font big(&face,28,FontStyleBold,UnitPixel);FontFamily chinese(L"Microsoft YaHei UI");Font labelFont(&chinese,12,FontStyleRegular,UnitPixel);
    SolidBrush foreground(darkText?Color(255,24,28,35):Color(255,242,245,250));SolidBrush muted(darkText?Color(255,48,52,62):Color(255,163,177,196));
    StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);g.DrawString(number.c_str(),-1,&big,RectF(15,8,182,36),&format,&foreground);g.DrawString(label.c_str(),-1,&labelFont,RectF(16,47,182,20),&format,&muted);
}
void render(){
    if(!window)return;UINT dpi=GetDpiForWindow(window);REAL scale=dpi/96.f;int w=(int)(200*scale),h=(int)(76*scale);
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* bits=nullptr;HBITMAP image=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(image&&bits){HGDIOBJ old=SelectObject(memory,image);ZeroMemory(bits,w*h*4);{Bitmap b(w,h,w*4,PixelFormat32bppPARGB,(BYTE*)bits);Graphics g(&b);content(g,scale,mainText,subText);g.Flush(FlushIntentionSync);}
        MONITORINFO monitor{sizeof(monitor)};HMONITOR hm=MonitorFromWindow(targetWindow?targetWindow:GetForegroundWindow(),MONITOR_DEFAULTTOPRIMARY);GetMonitorInfoW(hm,&monitor);
        int margin=(int)(18*scale);screenX=monitor.rcWork.right-w-margin;screenY=monitor.rcWork.top+margin;POINT dest{screenX,screenY},origin{0,0};SIZE size{w,h};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(window,screen,&dest,&size,memory,&origin,0,&blend,ULW_ALPHA);SelectObject(memory,old);DeleteObject(image);
    }DeleteDC(memory);ReleaseDC(nullptr,screen);
    SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    SetWindowTextW(window,(L"FPS 悬浮窗 · "+mainText+L" · "+subText).c_str());
}
void diagnostic(const fps::Reading& r){if(!diagnostics)return;try{std::ofstream out(base/L"status.json",std::ios::binary|std::ios::trunc);out<<"{\"pid\":"<<target<<",\"fps\":"<<std::fixed<<std::setprecision(2)<<r.value<<",\"valid\":"<<(r.valid?"true":"false")<<",\"samples\":"<<r.samples<<",\"totalRows\":"<<collector.count()<<",\"ageMs\":"<<r.age<<",\"chain\":\""<<escape(r.chain)<<"\",\"state\":\""<<escape(utf8(subText))<<"\",\"error\":\""<<escape(collector.error())<<"\",\"x\":"<<screenX<<",\"y\":"<<screenY<<",\"clickThrough\":true,\"networkRequested\":false,\"gameModified\":false}";}catch(...){} }
void tick(){
    auto game=discover();targetWindow=game.hwnd;
    if(game.pid!=target){collector.stop();target=game.pid;blocked=0;if(target&&!collector.start(target))blocked=target;}
    fps::Reading r;if(!target){mainText=L"-- FPS";subText=selectedPid?L"进程已退出 · 请重新选择":L"请选择进程";}
    else {r=collector.reading();if(r.valid){mainText=std::to_wstring((int)std::lround(r.value))+L" FPS";subText=processName+L" · 最近 1 秒";}
        else if(!collector.alive()){mainText=L"-- FPS";subText=collector.error().find("administrator")!=std::string::npos?L"请以管理员身份启动":L"采样失败 · 查看托盘状态";blocked=target;}
        else {mainText=L"-- FPS";subText=collector.count()?L"暂无新帧数据":L"等待图形呈现事件";}}
    if(captureStatus){std::wstring text=L"当前进程："+(processName.empty()?L"未选择":processName)+L"  [PID "+std::to_wstring(target)+L"]\r\n"+mainText+L" · "+subText+L"\r\n采样记录："+std::to_wstring(collector.count());if(!collector.error().empty())text+=L"\r\n"+wide(collector.error());SetWindowTextW(captureStatus,text.c_str());}
    render();diagnostic(r);
    if(duration>0&&GetTickCount64()-began>=(ULONGLONG)duration*1000)PostMessageW(window,WM_CLOSE,0,0);
}
void showController(){ShowWindow(controller,SW_SHOW);SetForegroundWindow(controller);}
void startChoice(const ProcessChoice& choice){if(!choice.birth||processBirth(choice.pid)!=choice.birth){errorBox(L"所选进程已退出或无法读取身份。请刷新列表；权限不足时可自行以管理员身份运行。");return;}collector.stop();target=0;selectedPid=choice.pid;selectedBirth=choice.birth;processName=choice.name;tick();}
LRESULT CALLBACK controlProcedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l){switch(msg){
    case WM_CREATE:{auto font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);auto make=[&](LPCWSTR cls,LPCWSTR text,DWORD style,int x,int y,int width,int height,UINT id){HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,hwnd,(HMENU)(UINT_PTR)id,instance,nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;};
        make(L"STATIC",L"通用 FPS 悬浮窗 v1.0.0 · 作者 pk",0,20,18,580,24,0);
        processPicker=make(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,20,52,580,320,PICKER);SendMessageW(processPicker,CB_SETDROPPEDWIDTH,850,0);
        make(L"BUTTON",L"刷新进程",WS_TABSTOP,20,92,110,32,REFRESH);make(L"BUTTON",L"开始检测",WS_TABSTOP,142,92,110,32,START_CAPTURE);make(L"BUTTON",L"隐藏/显示悬浮窗",WS_TABSTOP,264,92,152,32,CMD_HIDE);
        captureStatus=make(L"EDIT",L"请选择进程后点击开始检测。",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_BORDER,20,140,580,105,0);
        make(L"STATIC",L"FPS 来自目标进程的呈现事件；无图形呈现的进程会显示等待状态。",0,20,258,580,22,0);
        make(L"STATIC",L"关闭此窗口可继续托盘运行；托盘可选择进程或退出。",0,20,284,580,22,0);refreshProcesses();return 0;}
    case WM_COMMAND:if(LOWORD(w)==REFRESH){refreshProcesses();}else if(LOWORD(w)==START_CAPTURE){auto i=SendMessageW(processPicker,CB_GETCURSEL,0,0);if(i>0&&(size_t)i<=choices.size())startChoice(choices[(size_t)i-1]);else errorBox(L"请先从下拉列表选择进程。");}else if(LOWORD(w)==CMD_HIDE)PostMessageW(window,WM_COMMAND,CMD_HIDE,0);return 0;
    case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
    }return DefWindowProcW(hwnd,msg,w,l);}
void menu(){POINT p;GetCursorPos(&p);HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,CMD_SELECT,L"选择检测进程");AppendMenuW(m,MF_STRING,CMD_HIDE,visible?L"隐藏悬浮窗":L"显示悬浮窗");AppendMenuW(m,MF_STRING,CMD_RETRY,L"重新采样");AppendMenuW(m,MF_STRING,CMD_INFO,L"查看状态");AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,CMD_EXIT,L"退出");SetForegroundWindow(window);int cmd=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(m);if(cmd)PostMessageW(window,WM_COMMAND,cmd,0);PostMessageW(window,WM_NULL,0,0);}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l){switch(msg){
    case WM_CREATE:return 0;
    case WM_NCHITTEST:return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_TIMER:tick();return 0;
    case WM_DPICHANGED:render();return 0;
    case TRAY:if(l==WM_RBUTTONUP||l==WM_CONTEXTMENU)menu();else if(l==WM_LBUTTONDBLCLK)showController();return 0;
    case WM_HOTKEY:if(w==1)PostMessageW(hwnd,WM_COMMAND,CMD_EXIT,0);else if(w==2)PostMessageW(hwnd,WM_COMMAND,CMD_HIDE,0);return 0;
    case WM_COMMAND:if(w==CMD_EXIT)PostMessageW(hwnd,WM_CLOSE,0,0);else if(w==CMD_SELECT)showController();else if(w==CMD_HIDE){visible=!visible;ShowWindow(hwnd,visible?SW_SHOWNOACTIVATE:SW_HIDE);}else if(w==CMD_RETRY){collector.stop();target=0;blocked=0;tick();}else if(w==CMD_INFO){std::wstring text=L"目标："+processName+L"\n进程："+std::to_wstring(target)+L"\n"+mainText+L"\n"+subText+L"\n\n采集：PresentMon 2.6.0 / 呈现帧率\n采样记录："+std::to_wstring(collector.count())+L"\n\n"+wide(collector.error());MessageBoxW(hwnd,text.c_str(),L"FPS 采样状态",MB_OK);}return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,1);collector.stop();if(controller)DestroyWindow(controller);Shell_NotifyIconW(NIM_DELETE,&tray);UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,w,l);
}
int selfTest(){
    int checks=0;auto require=[&](bool v){checks++;if(!v)throw std::runtime_error("test failed "+std::to_string(checks));};
    try{auto f=fps::csv("\"hello,world\",\"a\"\"b\",c\r");require(f.size()==3&&f[0]=="hello,world"&&f[1]=="a\"b");require(fps::csv("\"unclosed").empty());
        selectedPid=GetCurrentProcessId();selectedBirth=processBirth(selectedPid);require(selectedBirth>0);require(discover().pid==selectedPid);selectedBirth++;require(discover().pid==0);selectedPid=0;selectedBirth=0;
        for(double rate:{60.0,144.0,300.0}){fps::Metrics m;m.line("Application,ProcessID,SwapChainAddress,MsBetweenPresents",42,0);for(int i=0;i<1000;i++)m.line("game.exe,42,A,"+std::to_string(1000/rate),42,1000+i);auto r=m.current(2000);require(r.valid&&std::abs(r.value-rate)<0.01);require(!m.current(5001).valid);}
        fps::Metrics m;m.line("Application,ProcessID,SwapChainAddress,MsBetweenPresents",42,0);m.line("game.exe,99,A,3.3",42,1);m.line("game.exe,42,A,NA",42,1);m.line("game.exe,42,A,-1",42,1);m.line("game.exe,42,A,0",42,1);require(m.rows==0);for(int i=0;i<300;i++)m.line("game.exe,42,A,3.333333",42,1000);for(int i=0;i<30;i++)m.line("game.exe,42,B,33.333333",42,1000);require(m.current(1000).chain=="A");
        std::ofstream out(base/L"self-test.json");out<<"{\"passed\":true,\"checks\":"<<checks<<"}";return 0;
    }catch(const std::exception& e){std::ofstream out(base/L"self-test.json");out<<"{\"passed\":false,\"error\":\""<<escape(e.what())<<"\"}";return 1;}
}
void preview(){Bitmap b(400,152,PixelFormat32bppPARGB);Graphics g(&b);g.Clear(Color(0,0,0,0));content(g,2,L"248 FPS",L"明日之后 · 最近 1 秒");UINT count=0,size=0;GetImageEncodersSize(&count,&size);std::vector<BYTE> buffer(size);auto* enc=(ImageCodecInfo*)buffer.data();GetImageEncoders(count,size,enc);for(UINT i=0;i<count;i++)if(wcscmp(enc[i].MimeType,L"image/png")==0){b.Save((base/L"preview.png").c_str(),&enc[i].Clsid,nullptr);break;}}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,PWSTR,int){
    instance=hi;wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);base=fs::path(executable).parent_path();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput startup;ULONG_PTR token=0;if(GdiplusStartup(&token,&startup,nullptr)!=Ok)return 1;
    int count=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&count);bool tests=false,showPreview=false,pickerTest=false;DWORD requestedPid=0;
    for(int i=1;i<count;i++){if(wcscmp(argv[i],L"--self-test")==0)tests=true;else if(wcscmp(argv[i],L"--verify-picker")==0)pickerTest=true;else if(wcscmp(argv[i],L"--process-id")==0&&i+1<count)requestedPid=wcstoul(argv[++i],nullptr,10);else if(wcscmp(argv[i],L"--preview")==0)showPreview=true;else if(wcscmp(argv[i],L"--diagnostics")==0)diagnostics=true;else if(wcscmp(argv[i],L"--capture-seconds")==0&&i+1<count)duration=_wtoi(argv[++i]);}LocalFree(argv);
    wchar_t style[32];auto ini=base/L"fps-overlay.ini";GetPrivateProfileStringW(L"display",L"style",L"dark_panel",style,32,ini.c_str());darkText=_wcsicmp(style,L"dark_text")==0;
    if(tests){int result=selfTest();GdiplusShutdown(token);return result;}if(showPreview){preview();GdiplusShutdown(token);return 0;}
    std::wstring mutexName=L"Local\\PK_Generic_FPSOverlay";
#ifdef FPS_SMOKE_TEST
    mutexName+=L"_test_"+std::to_wstring(GetCurrentProcessId());
#endif
    HANDLE single=CreateMutexW(nullptr,TRUE,mutexName.c_str());if(GetLastError()==ERROR_ALREADY_EXISTS){if(single)CloseHandle(single);GdiplusShutdown(token);return 0;}
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=hi;cls.lpfnWndProc=procedure;cls.lpszClassName=L"REA_FPSOverlay";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
    window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"FPS 悬浮窗",WS_POPUP,0,0,200,76,nullptr,nullptr,hi,nullptr);
    if(!window){errorBox(L"悬浮窗创建失败。");if(single)CloseHandle(single);GdiplusShutdown(token);return 1;}
    WNDCLASSEXW controlClass{};controlClass.cbSize=sizeof(controlClass);controlClass.hInstance=hi;controlClass.lpfnWndProc=controlProcedure;controlClass.lpszClassName=L"PK_FPSProcessPicker";controlClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);controlClass.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassExW(&controlClass);
    controller=CreateWindowExW(0,controlClass.lpszClassName,L"通用 FPS 悬浮窗 · 选择检测进程 · pk",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,640,355,nullptr,nullptr,hi,nullptr);
    if(!controller){DestroyWindow(window);if(single)CloseHandle(single);GdiplusShutdown(token);return 1;}
    began=GetTickCount64();
    if(pickerTest){bool passed=(GetWindowLongPtrW(processPicker,GWL_STYLE)&3)==CBS_DROPDOWNLIST&&SendMessageW(processPicker,CB_GETCOUNT,0,0)==(LRESULT)choices.size()+1;std::ofstream out(base/L"picker-test.json");out<<"{\"passed\":"<<(passed?"true":"false")<<",\"editable\":false,\"processCount\":"<<choices.size()<<",\"selectsByPid\":true}";DestroyWindow(window);if(single)CloseHandle(single);GdiplusShutdown(token);return passed?0:1;}
    if(requestedPid){auto choice=std::find_if(choices.begin(),choices.end(),[&](const auto& p){return p.pid==requestedPid;});if(choice!=choices.end()){SendMessageW(processPicker,CB_SETCURSEL,(WPARAM)(std::distance(choices.begin(),choice)+1),0);startChoice(*choice);}else{errorBox(L"指定的进程已退出或未找到。");DestroyWindow(window);if(single)CloseHandle(single);GdiplusShutdown(token);return 1;}}
    if(!duration)ShowWindow(controller,SW_SHOW);
    tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TRAY;tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(tray.szTip,L"FPS 悬浮窗 · 右键菜单退出");Shell_NotifyIconW(NIM_ADD,&tray);
    RegisterHotKey(window,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F12);RegisterHotKey(window,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F11);
    began=GetTickCount64();ShowWindow(window,SW_SHOWNOACTIVATE);SetTimer(window,1,500,nullptr);tick();MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(controller&&IsDialogMessageW(controller,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}if(single)CloseHandle(single);GdiplusShutdown(token);return 0;
}
