#include "capture.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace
{
void check(bool value) { if(!value) throw std::runtime_error("Capture test failed"); }
template<class F> void rejects(F f) { bool fail=false; try {f();} catch(const std::exception&) { fail=true; } check(fail); }
std::vector<std::uint8_t> read(const std::filesystem::path& p)
{ std::ifstream f{p,std::ios::binary}; return {std::istreambuf_iterator<char>{f},std::istreambuf_iterator<char>{}}; }
void decode(const std::filesystem::path& path,const simforge::renderer::CapturedFrame& expected)
{
    using Microsoft::WRL::ComPtr;
    check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
    {
        ComPtr<IWICImagingFactory> factory; check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))));
        ComPtr<IWICBitmapDecoder> decoder; check(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)));
        ComPtr<IWICBitmapFrameDecode> frame; check(SUCCEEDED(decoder->GetFrame(0,&frame))); UINT width=0,height=0; check(SUCCEEDED(frame->GetSize(&width,&height)) && width==expected.width && height==expected.height);
        ComPtr<IWICFormatConverter> convert; check(SUCCEEDED(factory->CreateFormatConverter(&convert)));
        check(SUCCEEDED(convert->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)));
        std::vector<std::uint8_t> pixels(expected.rgba.size()); check(SUCCEEDED(convert->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data())));
        check(pixels==expected.rgba);
    }
    CoUninitialize();
}
}
int main(int argc,char** argv)
{
    std::filesystem::path root;
    try
    {
        check(argc==3); const std::string_view c=argv[1]; root=std::filesystem::path{argv[2]}/std::to_string(GetCurrentProcessId());
        check(std::filesystem::create_directory(root));
        simforge::renderer::CapturedFrame f{3,2,{1,17,33,255,120,7,231,127,255,90,20,0,100,200,55,255,2,6,8,240,9,44,111,30}};
        const auto path=root/(c=="ppm"?"pixels.ppm":"pixels.png");
        if(c=="invalid")
        {
            auto bad=f; bad.rgba.pop_back(); rejects([&]{write_capture(bad,path);}); bad.width=0; rejects([&]{write_capture(bad,path);});
            rejects([&]{write_capture(f,root/"pixels.jpg");}); rejects([&]{write_capture(f,root/"missing"/"pixels.png");}); check(!std::filesystem::exists(path));
        }
        else
        {
            write_capture(f,path); const auto bytes=read(path);
            if(c=="png") decode(path,f);
            else if(c=="exclusive") { rejects([&]{write_capture(f,path);}); check(read(path)==bytes); }
            else if(c=="ppm") { const std::string header{"P6\n3 2\n255\n"}; check(bytes.size()==header.size()+18); for(std::size_t i=0;i<6;++i) for(std::size_t k=0;k<3;++k) check(bytes.at(header.size()+i*3+k)==f.rgba[i*4+k]); }
            else check(false);
        }
        std::filesystem::remove_all(root); return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
