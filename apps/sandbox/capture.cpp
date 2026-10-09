#include "capture.hpp"
#include <cstdlib>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
namespace
{
void exclusive_write(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes)
{
#ifdef _WIN32
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create new capture output (existing file or invalid destination)");
    std::size_t offset=0;
    while(offset<bytes.size())
    {
        const auto size=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,64*1024*1024)); DWORD written=0;
        if(!WriteFile(file,bytes.data()+offset,size,&written,nullptr) || written==0)
        { CloseHandle(file); throw std::runtime_error("Cannot write capture output; partial diagnostic retained"); }
        offset+=written;
    }
    if(!CloseHandle(file)) throw std::runtime_error("Cannot close capture output");
#else
    std::ofstream file{path,std::ios::binary|std::ios::noreplace};
    if(!file) throw std::runtime_error("Cannot create new capture output");
    file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!file) throw std::runtime_error("Cannot write capture output");
#endif
}
#ifdef _WIN32
void checked(HRESULT result,const char* action)
{ if(FAILED(result)) throw std::runtime_error(std::string{action}+" (HRESULT "+std::to_string(result)+")"); }
struct Apartment
{
    HRESULT result{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    Apartment() { if(result!=RPC_E_CHANGED_MODE) checked(result,"Initialize PNG codec COM apartment"); }
    ~Apartment() { if(SUCCEEDED(result)) CoUninitialize(); }
};
std::vector<std::uint8_t> png(const simforge::renderer::CapturedFrame& frame)
{
    Apartment apartment;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create Windows PNG codec");
    ComPtr<IStream> stream; checked(CreateStreamOnHGlobal(nullptr,TRUE,&stream),"Create PNG memory stream");
    ComPtr<IWICBitmapEncoder> encoder; checked(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create PNG encoder");
    checked(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> image; checked(encoder->CreateNewFrame(&image,nullptr),"Create PNG frame");
    checked(image->Initialize(nullptr),"Initialize PNG frame"); checked(image->SetSize(frame.width,frame.height),"Set PNG dimensions");
    auto format=GUID_WICPixelFormat32bppBGRA; checked(image->SetPixelFormat(&format),"Set PNG pixel format");
    if(format!=GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("PNG codec did not accept lossless BGRA8");
    auto bgra=frame.rgba;
    for(std::size_t i=0;i<bgra.size();i+=4) std::swap(bgra[i],bgra[i+2]);
    checked(image->WritePixels(frame.height,frame.width*4,static_cast<UINT>(bgra.size()),bgra.data()),"Encode PNG pixels");
    checked(image->Commit(),"Commit PNG frame"); checked(encoder->Commit(),"Commit PNG stream");
    STATSTG status{}; checked(stream->Stat(&status,STATFLAG_NONAME),"Read PNG size");
    if(status.cbSize.QuadPart>std::numeric_limits<ULONG>::max()) throw std::runtime_error("Encoded PNG exceeds supported size");
    LARGE_INTEGER zero{}; checked(stream->Seek(zero,STREAM_SEEK_SET,nullptr),"Rewind PNG stream");
    std::vector<std::uint8_t> output(static_cast<std::size_t>(status.cbSize.QuadPart)); ULONG read=0;
    checked(stream->Read(output.data(),static_cast<ULONG>(output.size()),&read),"Read encoded PNG");
    if(read!=output.size()) throw std::runtime_error("Truncated PNG memory stream");
    return output;
}
#endif
}
void write_capture(const simforge::renderer::CapturedFrame& frame,const std::filesystem::path& path)
{
    const auto started=std::chrono::steady_clock::now();
    const auto size=static_cast<std::uint64_t>(frame.width)*frame.height*4;
    if(frame.width==0 || frame.height==0 || frame.width>std::numeric_limits<std::uint32_t>::max()/4 ||
       size>std::numeric_limits<std::uint32_t>::max() || size!=frame.rgba.size())
        throw std::invalid_argument("Capture dimensions and RGBA8 pixel storage disagree");
    std::vector<std::uint8_t> bytes;
    if(path.extension()==".png")
    {
#ifdef _WIN32
        bytes=png(frame);
#else
        throw std::runtime_error("Direct PNG captures require Windows Imaging Component; use PPM on this platform");
#endif
    }
    else if(path.extension()==".ppm")
    {
        const auto header="P6\n"+std::to_string(frame.width)+" "+std::to_string(frame.height)+"\n255\n";
        bytes.assign(header.begin(),header.end()); bytes.reserve(bytes.size()+frame.rgba.size()/4*3);
        for(std::size_t i=0;i<frame.rgba.size();i+=4) bytes.insert(bytes.end(),frame.rgba.begin()+i,frame.rgba.begin()+i+3);
    }
    else throw std::invalid_argument("Capture output must be .png or .ppm");
    const auto encoded=std::chrono::steady_clock::now(); exclusive_write(path,bytes);
#ifdef _WIN32
    wchar_t flag[2]{};
    if(GetEnvironmentVariableW(L"SIMFORGE_CAPTURE_METRICS",flag,2)>0)
        std::cout<<"Capture metrics: bytes="<<bytes.size()<<" encode_ms="
            <<std::chrono::duration<double,std::milli>{encoded-started}.count()<<" write_ms="
            <<std::chrono::duration<double,std::milli>{std::chrono::steady_clock::now()-encoded}.count()<<'\n';
#endif
}
