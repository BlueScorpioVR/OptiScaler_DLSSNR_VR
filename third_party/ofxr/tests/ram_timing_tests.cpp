#include "xrfg/ram_timing.hpp"
#include "xrfg/d3d12_ram_timing.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool b, const char* text) { if(!b) throw std::runtime_error(text); }
std::string read(const fs::path& path) {
    std::ifstream f(path); return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc, char** argv) {
    if(argc==4 && std::string(argv[1])=="save") {
        fs::path saved; std::wstring error;
        return xrfg::save_ram_timing(std::stoul(argv[2]),argv[3],&saved,&error) ? 0 : 2;
    }
    const auto root=fs::temp_directory_path()/(L"xrfg-ram-v222-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root);
        if(argc==2 && std::string(argv[1])=="disabled") {
            { std::ofstream ini(root/L"ofxr_bridge.ini");
              ini<<"[diagnostics]\nlogging_enabled=0\ntiming_capture=0\n"; }
            auto& disabled=xrfg::bridge_flight_logger();
            disabled.shutdown(); disabled.initialize(root);
            require(!disabled.ram_enabled() && !disabled.enabled() && disabled.log_path().empty(),
                "disabled capture created recorder");
            using O=xrfg::BridgeFlightOperation;
            const auto token=disabled.begin(O::application_wait_frame);
            require(!token.sequence && !token.start_counter,"disabled begin acquired timestamp/token");
            disabled.end(token,O::application_wait_frame,0);
            disabled.event(O::three_slot_handoff,0,1,0,0);
            // Null device/queue/lists deliberately prove the disabled helper
            // issues no calibration, allocations, EndQuery, Resolve or readback.
            xrfg::D3D12RamTiming<2> source;
            xrfg::D3D12RamTiming<5> synthesis;
            source.initialize(nullptr,nullptr,O::gpu_history);
            synthesis.initialize(nullptr,nullptr,O::gpu_synthesis);
            source.mark(nullptr,0); source.resolve(nullptr); source.submitted(1,1); source.harvest();
            synthesis.mark(nullptr,0); synthesis.resolve(nullptr); synthesis.submitted(1,1); synthesis.harvest();
            fs::path saved; std::wstring error;
            require(!xrfg::save_ram_timing(GetCurrentProcessId(),root,&saved,&error),"disabled mapping exists");
            for(const auto& file:fs::directory_iterator(root))
                require(file.path().extension()==L".ini","disabled diagnostics wrote a file");
            disabled.shutdown(); fs::remove_all(root);
            std::cout<<"RAM OFF: no mapping, event timestamp, file or GPU query/readback: PASS\n";
            return 0;
        }
        { std::ofstream ini(root/L"ofxr_bridge.ini");
          ini<<"[diagnostics]\nlogging_enabled=1\ntiming_capture=1\n"; }
        xrfg::BridgeFlightLogger logger; logger.initialize(root);
        require(logger.ram_enabled() && !logger.enabled() && logger.log_path().empty(),"RAM must not enable normal diagnostics");
        using O=xrfg::BridgeFlightOperation;
        require(!logger.begin(O::nvidia_gpu_stages).sequence,"unselected event captured");
        auto token=logger.begin(O::application_wait_frame,11,22,33);
        require((token.sequence&xrfg::kRamTimingToken)!=0,"missing RAM begin");
        logger.end(token,O::application_wait_frame,0,44,55,66);
        require(fs::is_empty(root)==false,"fixture missing");
        unsigned files=0; for(auto& p:fs::directory_iterator(root)) {++files; require(p.path().extension()==L".ini","premature disk write");}
        require(files==1,"recorder wrote before save");
        require(xrfg::pause_ram_timing(GetCurrentProcessId(),true),"pause failed");
        require(!logger.begin(O::application_wait_frame).sequence,"pause didn't stop records");
        require(xrfg::pause_ram_timing(GetCurrentProcessId(),false),"resume failed");
        // Force wrap while carrying a predictable payload.
        for(unsigned n=0;n<80000;++n) logger.event(O::three_slot_handoff,0,n,n-1,215);
        fs::path saved; std::wstring error;
        require(xrfg::save_ram_timing(GetCurrentProcessId(),root,&saved,&error),"save failed");
        const auto csv=read(saved);
        require(csv.find("schema=222")!=std::string::npos &&
            csv.find("three_slot_handoff,0,79999,79998,215")!=std::string::npos,
            "wrapped ring payload lost");
        require(csv.find("three_slot_handoff,0,1,0,215")==std::string::npos,"overwritten ring entry retained");
        // Cross-process reader while multiple writer threads are running.
        std::atomic<bool> stop{};
        std::atomic<std::uint64_t> count{};
        std::vector<std::jthread> writers;
        for(unsigned i=0;i<3;++i) writers.emplace_back([&]{
            while(!stop.load()) {
                auto t=logger.begin(O::timing_worker_run,123,122,0);
                logger.end(t,O::timing_worker_run,0,123,122,0); ++count;
            }
        });
        wchar_t exe[32768]{}; GetModuleFileNameW(nullptr,exe,32768);
        std::wstring command=L"\""+std::wstring(exe)+L"\" save "+std::to_wstring(GetCurrentProcessId())+
            L" \""+root.wstring()+L"\"";
        STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        const bool created=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;
        DWORD result=99;
        if(created) {
            if(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess,&result);
            CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        }
        stop=true;writers.clear();
        require(created && result==0 && count>0,"concurrent cross-process capture failed");
        require(logger.begin(O::application_end_frame).sequence!=0,"capture did not resume");
        require(xrfg::pause_ram_timing(GetCurrentProcessId(),true),"second pause");
        require(xrfg::save_ram_timing(GetCurrentProcessId(),root,&saved,&error),"save paused");
        require(!logger.begin(O::application_wait_frame).sequence,"save changed manual pause state");
        xrfg::pause_ram_timing(GetCurrentProcessId(),false);
        const auto before=std::chrono::steady_clock::now();
        for(unsigned n=0;n<200000;++n) logger.event(O::three_slot_handoff,0,n,0,0);
        const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-before).count();
        std::cout<<"RAM capture record mean (local CPU test, not game validation): "<<us/200000<<" us\n";
        logger.shutdown();
        require(!xrfg::pause_ram_timing(GetCurrentProcessId(),true),"mapping leaked after shutdown");
        fs::remove_all(root);
        std::cout<<"RAM wrap, no disk before save, cross-process concurrent snapshot, pause/resume: PASS\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
