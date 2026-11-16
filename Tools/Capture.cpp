#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
int main(int argc,char** argv){
 juce::ScopedJuceInitialiser_GUI init;
 if(argc!=3)return 1;
 juce::AudioPluginFormatManager manager;
 manager.addDefaultFormats();
 juce::OwnedArray<juce::PluginDescription> types;
 for(auto* format:manager.getFormats())if(format->getName()=="VST3")format->findAllTypesForFile(types,juce::String::fromUTF8(argv[1]));
 if(types.isEmpty()){std::cerr<<"No VST3 types found";return 2;}
 juce::String error;
 auto plugin=manager.createPluginInstance(*types[0],44100,512,error);
 if(!plugin){std::cerr<<error;return 3;}
 plugin->prepareToPlay(44100,512);
 std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
 if(!editor)return 4;
 editor->setVisible(true);
 auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
 juce::File file=juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));
 file.deleteFile();juce::FileOutputStream stream(file);juce::PNGImageFormat png;
 if(!stream.openedOk()||!png.writeImageToStream(image,stream))return 5;
 stream.flush();return 0;
}
