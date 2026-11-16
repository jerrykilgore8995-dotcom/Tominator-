#include <juce_audio_utils/juce_audio_utils.h>
#include <windows.h>
#include <iostream>
int main(int argc,char** argv){
 juce::ScopedJuceInitialiser_GUI init;
 if(argc!=3)return 1;
 juce::AudioPluginFormatManager manager;manager.addDefaultFormats();
 juce::OwnedArray<juce::PluginDescription> types;
 for(auto* format:manager.getFormats())if(format->getName()=="VST3")format->findAllTypesForFile(types,juce::String::fromUTF8(argv[1]));
 if(types.isEmpty())return 2;
 juce::String error;auto plugin=manager.createPluginInstance(*types[0],44100,512,error);
 if(!plugin){std::cerr<<error;return 3;}
 plugin->prepareToPlay(44100,512);
 std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());if(!editor)return 4;
 editor->setTopLeftPosition(0,0);editor->addToDesktop(0);editor->setVisible(true);editor->toFront(true);
 auto until=GetTickCount64()+1800;
 while(GetTickCount64()<until){MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}Sleep(10);}
 HWND hwnd=static_cast<HWND>(editor->getWindowHandle());if(!hwnd)return 6;
 RECT rc{};GetClientRect(hwnd,&rc);int w=rc.right,h=rc.bottom;if(w<100||h<100)return 7;
 HDC screen=GetDC(hwnd), mem=CreateCompatibleDC(screen);
 BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=w;bmi.bmiHeader.biHeight=-h;bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
 void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&bmi,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap||!pixels)return 8;
 auto old=SelectObject(mem,bitmap);
 auto capture=[&](){juce::Image img(juce::Image::RGB,w,h,true);auto* data=static_cast<unsigned char*>(pixels);for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto* b=data+4*(y*w+x);img.setPixelAt(x,y,juce::Colour(b[2],b[1],b[0]));}return img;};
 auto good=[](const juce::Image& img){int bright=0;for(int y=0;y<img.getHeight();y+=4)for(int x=0;x<img.getWidth();x+=4)if(img.getPixelAt(x,y).getBrightness()>.55f)++bright;return bright>150;};
 PrintWindow(hwnd,mem,3);auto image=capture();
 if(!good(image)){BitBlt(mem,0,0,w,h,screen,0,0,SRCCOPY);image=capture();}
 SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(hwnd,screen);
 if(!good(image)){std::cerr<<"Native VST3 screenshot is blank";return 9;}
 juce::File file=juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));file.deleteFile();juce::FileOutputStream stream(file);juce::PNGImageFormat png;
 if(!stream.openedOk()||!png.writeImageToStream(image,stream))return 5;
 stream.flush();editor->removeFromDesktop();return 0;
}
