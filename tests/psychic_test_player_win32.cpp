#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
constexpr int ID_INPUT=101, ID_OUTPUT=102, ID_BROWSE_INPUT=103, ID_BROWSE_OUTPUT=104,
              ID_MODE=105, ID_PROCESS=106, ID_OPEN=107, ID_LOG=108;
HWND g_input{},g_output{},g_mode{},g_log{},g_process{},g_open{};
std::wstring quote(const std::wstring& s) {
    std::wstring q=L"\"";
    for(wchar_t c:s){ if(c==L'\"') q+=L"\\\""; else q+=c; }
    q+=L"\""; return q;
}
std::wstring get_text(HWND h) {
    int n=GetWindowTextLengthW(h); std::wstring s(static_cast<size_t>(n)+1,L'\0');
    GetWindowTextW(h,s.data(),n+1); s.resize(n); return s;
}
void set_log(const std::wstring& s){SetWindowTextW(g_log,s.c_str());}
std::wstring browse_file(bool save,const std::wstring& initial) {
    wchar_t path[MAX_PATH]{}; if(!initial.empty()) wcsncpy_s(path,initial.c_str(),_TRUNCATE);
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=GetParent(g_input);
    ofn.lpstrFile=path; ofn.nMaxFile=MAX_PATH; ofn.lpstrFilter=L"Wave audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    ofn.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    ofn.lpstrDefExt=L"wav";
    if(save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn)) return path;
    return {};
}
void browse_input(){auto p=browse_file(false,get_text(g_input));if(!p.empty()){SetWindowTextW(g_input,p.c_str());if(get_text(g_output).empty()){p+=L".psychic.wav";SetWindowTextW(g_output,p.c_str());}}}
void browse_output(){auto p=browse_file(true,get_text(g_output));if(!p.empty())SetWindowTextW(g_output,p.c_str());}
void process_audio(){
    auto in=get_text(g_input), out=get_text(g_output);
    if(in.empty()||out.empty()){MessageBoxW(GetParent(g_input),L"Choose an input WAV and output WAV first.",L"Psychic Test Player",MB_ICONWARNING);return;}
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring dir=exe;auto slash=dir.find_last_of(L"\\/");dir=slash==std::wstring::npos?L".":dir.substr(0,slash);
    std::wstring sim=dir+L"\\psychic_playback_simulator.exe";
    DWORD attr=GetFileAttributesW(sim.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES){MessageBoxW(GetParent(g_input),L"psychic_playback_simulator.exe is missing. Extract the complete ZIP and keep both EXEs together.",L"Psychic Test Player",MB_ICONERROR);return;}
    int sel=static_cast<int>(SendMessageW(g_mode,CB_GETCURSEL,0,0));const wchar_t* modes[]={L"both",L"none",L"bass",L"crest"};
    std::wstring cmd=quote(sim)+L" "+quote(in)+L" "+quote(out)+L" "+modes[sel<0?3:sel];
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE readPipe{},writePipe{};
    if(!CreatePipe(&readPipe,&writePipe,&sa,0)){set_log(L"Could not create process output pipe.");return;}
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    si.hStdOutput=writePipe;si.hStdError=writePipe;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};std::vector<wchar_t> mutableCmd(cmd.begin(),cmd.end());mutableCmd.push_back(0);
    EnableWindow(g_process,FALSE);EnableWindow(g_open,FALSE);set_log(L"Processing audio with Psychic...\r\nPlease wait; long tracks may take a little time.");
    BOOL ok=CreateProcessW(nullptr,mutableCmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi);
    CloseHandle(writePipe);
    if(!ok){CloseHandle(readPipe);EnableWindow(g_process,TRUE);set_log(L"Could not start the DSP simulator.");return;}
    WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);
    std::string bytes;char buf[4096];DWORD got=0;while(ReadFile(readPipe,buf,sizeof(buf),&got,nullptr)&&got)bytes.append(buf,got);
    CloseHandle(readPipe);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    int needed=MultiByteToWideChar(CP_UTF8,0,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    std::wstring report(static_cast<size_t>(needed),L'\0');if(needed)MultiByteToWideChar(CP_UTF8,0,bytes.data(),static_cast<int>(bytes.size()),report.data(),needed);
    if(code==0){report+=L"\r\n\r\nProcessing complete. CSV diagnostics saved beside output.";EnableWindow(g_open,TRUE);}
    else report+=L"\r\n\r\nProcessing failed. Check that the WAV is supported and contains enough active audio.";
    set_log(report);EnableWindow(g_process,TRUE);
}
void open_output(){auto p=get_text(g_output);if(!p.empty())ShellExecuteW(nullptr,L"open",p.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
HWND add(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){
 return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
}
LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM wp,LPARAM lp){
 if(m==WM_CREATE){
  HFONT font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  add(h,L"STATIC",L"Input WAV",0,16,16,80,22,0);g_input=add(h,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,100,14,460,24,ID_INPUT);
  add(h,L"BUTTON",L"Browse...",BS_PUSHBUTTON,570,14,90,24,ID_BROWSE_INPUT);
  add(h,L"STATIC",L"Output WAV",0,16,48,80,22,0);g_output=add(h,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,100,46,460,24,ID_OUTPUT);
  add(h,L"BUTTON",L"Save as...",BS_PUSHBUTTON,570,46,90,24,ID_BROWSE_OUTPUT);
  add(h,L"STATIC",L"Hint mode",0,16,82,80,22,0);g_mode=add(h,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,100,78,160,150,ID_MODE);
  const wchar_t* opts[]={L"Both hints",L"No hints",L"Bass only",L"Crest only"};
  for(int i=0;i<4;i++)SendMessageW(g_mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(opts[i]));
  SendMessageW(g_mode,CB_SETCURSEL,0,0);
  g_process=add(h,L"BUTTON",L"Process track",BS_PUSHBUTTON,280,78,150,30,ID_PROCESS);
  g_open=add(h,L"BUTTON",L"Open processed audio",BS_PUSHBUTTON,440,78,220,30,ID_OPEN);EnableWindow(g_open,FALSE);
  add(h,L"STATIC",L"Results and analysis",0,16,120,180,22,0);
  g_log=add(h,L"EDIT",L"Select a WAV, choose a hint mode, then click Process track.\r\nThe app writes a processed WAV and a CSV telemetry report.",WS_BORDER|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL,16,144,644,290,ID_LOG);
  for(HWND c=GetWindow(h,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT))SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
  return 0;
 }
 if(m==WM_COMMAND){switch(LOWORD(wp)){case ID_BROWSE_INPUT:browse_input();return 0;case ID_BROWSE_OUTPUT:browse_output();return 0;case ID_PROCESS:process_audio();return 0;case ID_OPEN:open_output();return 0;}}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int show){
 const wchar_t* name=L"PsychicTestPlayerWindow";WNDCLASSW wc{};wc.lpfnWndProc=wndproc;wc.hInstance=inst;wc.lpszClassName=name;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
 HWND h=CreateWindowExW(0,name,L"Psychic — Audio Test Player",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, CW_USEDEFAULT,CW_USEDEFAULT,700,490,nullptr,nullptr,inst,nullptr);
 if(!h)return 1;ShowWindow(h,show);UpdateWindow(h);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return static_cast<int>(msg.wParam);
}
