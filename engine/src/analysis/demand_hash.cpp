#include <simforge/analysis/demand_calibration.hpp>
#include <array>
#include <bit>
#include <iomanip>
#include <sstream>

namespace simforge::analysis::demand {
std::string sha256(std::string_view input,const simulation::ExecutionControl* control) {
    // FIPS 180-4 SHA-256. Defined uint32 wraparound and big-endian byte decoding.
    constexpr std::array<std::uint32_t,64> k{
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    if(input.size()>max_csv_bytes)throw std::length_error("SHA-256 input exceeds bounded content limit");
    std::array<std::uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const auto length=static_cast<std::uint64_t>(input.size())*8;
    const auto blocks=(input.size()+9+63)/64;
    for(std::size_t block=0;block<blocks;++block) {
        if(control && block%1024==0)control->check();
        std::array<std::uint32_t,64> w{};
        for(std::size_t i=0;i<64;++i) {
            const auto index=block*64+i;
            const auto byte=index<input.size()?static_cast<unsigned char>(input[index]):
                index==input.size()?128U:index>=blocks*64-8?static_cast<unsigned>((length>>((blocks*64-1-index)*8))&255):0U;
            w[i/4]|=byte<<((3-i%4)*8);
        }
        for(std::size_t i=16;i<64;++i) {
            const auto s0=std::rotr(w[i-15],7)^std::rotr(w[i-15],18)^(w[i-15]>>3);
            const auto s1=std::rotr(w[i-2],17)^std::rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for(std::size_t i=0;i<64;++i) {
            const auto s1=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
            const auto t1=v+s1+((e&f)^(~e&g))+k[i]+w[i];
            const auto s0=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
            const auto t2=s0+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto value:h)out<<std::setw(8)<<value;return out.str();
}
}
