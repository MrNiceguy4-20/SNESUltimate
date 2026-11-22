#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>
namespace stateio {
class Writer {
public:
    template<class T> void pod(const T& v){ static_assert(std::is_trivially_copyable<T>::value,"pod"); const auto* p=reinterpret_cast<const uint8_t*>(&v); data_.insert(data_.end(),p,p+sizeof(T)); }
    template<class T,std::size_t N> void array(const std::array<T,N>& a){ static_assert(std::is_trivially_copyable<T>::value,"array"); const auto* p=reinterpret_cast<const uint8_t*>(a.data()); data_.insert(data_.end(),p,p+sizeof(T)*N); }
    template<class T> void vector(const std::vector<T>& v){ static_assert(std::is_trivially_copyable<T>::value,"vector"); uint32_t n=static_cast<uint32_t>(v.size()); pod(n); if(n){const auto* p=reinterpret_cast<const uint8_t*>(v.data());data_.insert(data_.end(),p,p+sizeof(T)*n);} }
    std::vector<uint8_t> take(){return std::move(data_);}
private: std::vector<uint8_t> data_;
};
class Reader {
public:
    Reader(const std::vector<uint8_t>& d):p_(d.data()),n_(d.size()){}
    template<class T> bool pod(T& v){ static_assert(std::is_trivially_copyable<T>::value,"pod"); if(n_<sizeof(T))return false;std::memcpy(&v,p_,sizeof(T));p_+=sizeof(T);n_-=sizeof(T);return true; }
    template<class T,std::size_t N> bool array(std::array<T,N>& a){ static_assert(std::is_trivially_copyable<T>::value,"array");constexpr auto bytes=sizeof(T)*N;if(n_<bytes)return false;std::memcpy(a.data(),p_,bytes);p_+=bytes;n_-=bytes;return true; }
    template<class T> bool vector(std::vector<T>& v,std::size_t max=16*1024*1024){ static_assert(std::is_trivially_copyable<T>::value,"vector");uint32_t count=0;if(!pod(count)||count>max/sizeof(T)||n_<sizeof(T)*count)return false;v.resize(count);if(count)std::memcpy(v.data(),p_,sizeof(T)*count);p_+=sizeof(T)*count;n_-=sizeof(T)*count;return true; }
    bool done()const{return n_==0;}
private: const uint8_t* p_;std::size_t n_;
};
}
