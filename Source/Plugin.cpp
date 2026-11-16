#include <JuceHeader.h>
#include <array>
#include <cmath>
using namespace juce;
using AP=AudioProcessorValueTreeState;
static const std::array<std::array<int,3>,6> routes{{{{0,1,2}},{{0,2,1}},{{1,0,2}},{{1,2,0}},{{2,0,1}},{{2,1,0}}}};
static const StringArray names{"COMP","EQ","TRANSIENT"};
class Engine: public AudioProcessor {
public:
 AP p{*this,nullptr,"Tominator",layout()};
 std::atomic<float> gr{0};
 static AP::ParameterLayout layout(){
 AP::ParameterLayout l;
 auto f=[&](String id,String name,float a,float b,float d,float step=0.1f,float skew=1.f){l.add(std::make_unique<AudioParameterFloat>(ParameterID{id,1},name,NormalisableRange<float>(a,b,step,skew),d));};
 auto b=[&](String id,String n){l.add(std::make_unique<AudioParameterBool>(ParameterID{id,1},n,false));};
 auto c=[&](String id,String n,StringArray a,int d=0){l.add(std::make_unique<AudioParameterChoice>(ParameterID{id,1},n,a,d));};
 f("input","Input",-24,24,0); f("output","Output",-24,24,0);
 b("comp","Compressor in"); b("eq","EQ in"); b("trans","Transient in");
 f("threshold","Threshold",-60,0,-18); f("attack","Attack ms",1,30,15); f("release","Release ms",50,200,100);
 c("ratio","Ratio",{"3:1","4:1","5:1","6:1"},1); c("knee","Knee",{"Hard","Soft 8 dB"}); f("makeup","Makeup",-12,24,0);
 c("hp","HPF",{"Off","30","60","100","150"}); c("lp","LPF",{"Off","20k","16k","12k","10k"});
 float lo[]={0,100,600,2000,6000},hi[]={500,1000,3000,8000,20000},def[]={100,300,1200,4000,12000};
 for(int i=0;i<5;++i){auto k=String(i); f("freq"+k,"Frequency "+k,lo[i],hi[i],def[i],1,.45f);f("gain"+k,"Gain "+k,-16,16,0);if(i>0&&i<4)f("q"+k,"Q "+k,.15f,12,1,.01f,.35f);}
 f("punch","Transient",-15,15,0);f("sustain","Sustain",-15,15,0);
 c("order","Order",{"COMP EQ TRANSIENT","COMP TRANSIENT EQ","EQ COMP TRANSIENT","EQ TRANSIENT COMP","TRANSIENT COMP EQ","TRANSIENT EQ COMP"});
 return l;}
 Engine():AudioProcessor(BusesProperties().withInput("Input",AudioChannelSet::stereo(),true).withOutput("Output",AudioChannelSet::stereo(),true)){}
 float v(String id){return p.getRawParameterValue(id)->load();}
 const String getName()const override{return "Tominator";} bool acceptsMidi()const override{return false;} bool producesMidi()const override{return false;} double getTailLengthSeconds()const override{return 0;}
 int getNumPrograms()override{return 1;} int getCurrentProgram()override{return 0;} void setCurrentProgram(int)override{} const String getProgramName(int)override{return {};} void changeProgramName(int,const String&)override{}
 bool isBusesLayoutSupported(const BusesLayout& l)const override{return l.getMainInputChannelSet()==l.getMainOutputChannelSet()&&(l.getMainOutputChannelSet()==AudioChannelSet::mono()||l.getMainOutputChannelSet()==AudioChannelSet::stereo());}
 struct Biquad{double b0=1,b1=0,b2=0,a1=0,a2=0,z1[2]{},z2[2]{};void set(const dsp::IIR::Coefficients<double>& c){auto* a=c.getRawCoefficients();b0=a[0];b1=a[1];b2=a[2];a1=a[3];a2=a[4];} float run(float x,int c){double y=b0*x+z1[c];z1[c]=b1*x-a1*y+z2[c];z2[c]=b2*x-a2*y;return float(y);}void reset(){for(int i=0;i<2;++i)z1[i]=z2[i]=0;}};
 std::array<Biquad,7> filters; std::array<bool,7> active{}; double sr=44100,level=0,fast=0,slow=0,body=0; bool wasComp=false,wasEQ=false,wasTrans=false;
 void prepareToPlay(double s,int)override{sr=s;level=fast=slow=body=0;for(auto& f:filters)f.reset();active.fill(false);wasComp=wasEQ=wasTrans=false;} void releaseResources()override{}
 void processBlock(AudioBuffer<float>& buf,MidiBuffer&)override{
 ScopedNoDenormals nd;auto channels=buf.getNumChannels(),n=buf.getNumSamples();
 float in=v("input"),out=v("output"); if(in!=0)buf.applyGain(Decibels::decibelsToGain(in));
 bool enabled[]={v("comp")>0.5f,v("eq")>0.5f,v("trans")>0.5f};
 if(!enabled[0]){level=0;gr=0;}if(!enabled[1]&&wasEQ){for(auto& f:filters)f.reset();active.fill(false);}if(!enabled[2])fast=slow=body=0;
 for(int id:routes[size_t(jlimit(0,5,int(v("order"))))]){
 if(!enabled[id])continue;
 if(id==0){double a=std::exp(-1/(sr*v("attack")*.001)),r=std::exp(-1/(sr*v("release")*.001)),t=v("threshold"),ratio=3+v("ratio"),w=v("knee")>0?8:0,make=v("makeup");
 for(int i=0;i<n;++i){float peak=0;for(int c=0;c<channels;++c)peak=jmax(peak,std::abs(buf.getSample(c,i)));double x=Decibels::gainToDecibels(double(peak),-120.0)-t;double over=w>0&&x>-w*.5&&x<w*.5?(x+w*.5)*(x+w*.5)/(2*w):std::max(0.0,x);double target=over*(1-1/ratio);double coeff=target>level?a:r;level=coeff*level+(1-coeff)*target;float gain=float(Decibels::decibelsToGain(make-level));for(int c=0;c<channels;++c)buf.setSample(c,i,buf.getSample(c,i)*gain);}gr=float(level);}
 if(id==1){using C=dsp::IIR::Coefficients<double>;auto freq=[&](double f){return jlimit(1.0,sr*.475,f);};auto set=[&](int j,bool on,C::Ptr coeff){if(on&&coeff)filters[j].set(*coeff);if(on!=active[j])filters[j].reset();active[j]=on;};
 int h=int(v("hp")),l=int(v("lp"));double hf[]={30,30,60,100,150},lf[]={20000,20000,16000,12000,10000};
 set(0,h!=0,h?C::makeHighPass(sr,freq(hf[h]),.70710678):nullptr);set(6,l!=0,l?C::makeLowPass(sr,freq(lf[l]),.70710678):nullptr);
 for(int j=0;j<5;++j){String k(j);double g=v("gain"+k),f=v("freq"+k),a=Decibels::decibelsToGain(g);C::Ptr co;if(g!=0&&f>0){if(j==0)co=C::makeLowShelf(sr,freq(f),.70710678,a);else if(j==4)co=C::makeHighShelf(sr,freq(f),.70710678,a);else {double q=jlimit(.1,24.0,double(v("q"+k))*(.65+.085*std::abs(g)));co=C::makePeakFilter(sr,freq(f),q,a);}}set(j+1,g!=0&&f>0,co);}
 for(int j=0;j<7;++j)if(active[j])for(int c=0;c<channels;++c){auto* x=buf.getWritePointer(c);for(int i=0;i<n;++i)x[i]=filters[j].run(x[i],c);}}
 if(id==2){double punch=v("punch"),sus=v("sustain");if(punch==0&&sus==0){fast=slow=body=0;continue;}double fa=std::exp(-1/(sr*.001)),fr=std::exp(-1/(sr*.025)),sa=std::exp(-1/(sr*.025)),rr=std::exp(-1/(sr*.12)),ba=std::exp(-1/(sr*.008));
 for(int i=0;i<n;++i){double x=0;for(int c=0;c<channels;++c)x=std::max(x,double(std::abs(buf.getSample(c,i))));double a=x>fast?fa:fr,b=x>slow?sa:rr;fast=a*fast+(1-a)*x;slow=b*slow+(1-b)*x;double onset=jlimit(0.0,1.0,(fast-slow)/(fast+1e-8));body=ba*body+(1-ba)*jlimit(0.0,1.0,(slow-fast*.35)/(slow+1e-8));float gain=float(Decibels::decibelsToGain(punch*onset+sus*body*(1-onset)));for(int c=0;c<channels;++c)buf.setSample(c,i,buf.getSample(c,i)*gain);}}
 }
 wasComp=enabled[0];wasEQ=enabled[1];wasTrans=enabled[2];if(out!=0)buf.applyGain(Decibels::decibelsToGain(out));
 }
 void getStateInformation(MemoryBlock& b)override{auto xml=p.copyState().createXml();copyXmlToBinary(*xml,b);}void setStateInformation(const void* d,int n)override{if(auto xml=getXmlFromBinary(d,n))if(xml->hasTagName(p.state.getType()))p.replaceState(ValueTree::fromXml(*xml));}
 bool hasEditor()const override{return true;} AudioProcessorEditor* createEditor()override;
};
class RackLook:public LookAndFeel_V4{
public: void drawRotarySlider(Graphics& g,int x,int y,int w,int h,float pos,float start,float end,Slider& s)override{
 float r=jmin(w,h)*.39f,cx=x+w*.5f,cy=y+h*.5f; Rectangle<float> b(cx-r,cy-r,2*r,2*r);
 g.setColour(Colours::black.withAlpha(.65f));g.fillEllipse(b.translated(1,3).expanded(3));
 g.setGradientFill(ColourGradient(Colour(0xff85898c),cx-r,cy-r,Colour(0xff15191c),cx+r,cy+r,false));g.fillEllipse(b.expanded(2));
 auto base=s.findColour(Slider::rotarySliderFillColourId);g.setGradientFill(ColourGradient(base.brighter(.5f),cx-r,cy-r,base.darker(.7f),cx+r,cy+r,false));g.fillEllipse(b.reduced(3));
 for(int i=0;i<24;++i){float a=i*MathConstants<float>::twoPi/24;g.setColour(Colours::black.withAlpha(.35f));g.drawLine(cx+std::sin(a)*(r-3),cy-std::cos(a)*(r-3),cx+std::sin(a)*(r-7),cy-std::cos(a)*(r-7),1);}
 for(int i=0;i<=10;++i){float a=start+(end-start)*i/10;g.setColour(Colour(0xffbec4c7));g.drawLine(cx+std::sin(a)*(r+5),cy-std::cos(a)*(r+5),cx+std::sin(a)*(r+9),cy-std::cos(a)*(r+9),1);}
 float a=start+pos*(end-start);g.setColour(Colours::ivory);g.drawLine(cx+std::sin(a)*r*.35f,cy-std::cos(a)*r*.35f,cx+std::sin(a)*r*.8f,cy-std::cos(a)*r*.8f,2.5f);
 }};
class Editor:public AudioProcessorEditor,private Timer{
 Engine& e; RackLook look;
 std::vector<std::unique_ptr<Slider>> knobs;
 std::vector<std::unique_ptr<Label>> labels;
 std::vector<std::unique_ptr<TextButton>> buttons;
 std::vector<std::unique_ptr<AP::SliderAttachment>> sa;
 std::vector<std::unique_ptr<AP::ButtonAttachment>> ba;
 ComboBox order; int maskLast=-1,routeLast=-1; std::vector<int> map;
 void label(String t,int x,int y,int w,int h){auto z=std::make_unique<Label>();z->setText(t,dontSendNotification);z->setJustificationType(Justification::centred);z->setColour(Label::textColourId,Colour(0xffede8dc));z->setFont(Font(12.f,Font::bold));z->setBounds(x,y,w,h);addAndMakeVisible(*z);labels.push_back(std::move(z));}
 void knob(String id,String caption,int x,int y,int w=74,int h=90,Colour colour=Colour(0xff4e555b)){
 auto z=std::make_unique<Slider>();z->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);z->setRotaryParameters(3.9f,8.65f,true);z->setTextBoxStyle(Slider::TextBoxBelow,false,w-6,18);z->setColour(Slider::rotarySliderFillColourId,colour);z->setBounds(x,y+14,w,h-14);addAndMakeVisible(*z);label(caption,x,y,w,15);sa.push_back(std::make_unique<AP::SliderAttachment>(e.p,id,*z));knobs.push_back(std::move(z));}
 void toggle(String id,String title,int x,int y,int w=72){auto z=std::make_unique<TextButton>(title);z->setClickingTogglesState(true);z->setColour(TextButton::buttonColourId,Colour(0xff24292c));z->setColour(TextButton::buttonOnColourId,Colour(0xff386e48));z->setBounds(x,y,w,25);addAndMakeVisible(*z);ba.push_back(std::make_unique<AP::ButtonAttachment>(e.p,id,*z));buttons.push_back(std::move(z));}
 void choices(String id,StringArray a,int x,int y,int w){for(int i=0;i<a.size();++i){auto z=std::make_unique<TextButton>(a[i]);z->setBounds(x+i*w,y,w-3,24);z->setColour(TextButton::buttonColourId,Colour(0xff272d33));z->setColour(TextButton::buttonOnColourId,Colour(0xff497c86));auto* ptr=z.get();z->onClick=[this,id,i]{auto* p=e.p.getParameter(id);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(i)));p->endChangeGesture();};addAndMakeVisible(*z);choiceButtons.push_back({ptr,id,i});buttons.push_back(std::move(z));}}
 struct ChoiceButton{TextButton* b;String id;int index;};std::vector<ChoiceButton> choiceButtons;
 void timerCallback()override{for(auto& c:choiceButtons)c.b->setToggleState(int(e.v(c.id))==c.index,dontSendNotification);int m=(e.v("comp")>.5f?1:0)|(e.v("eq")>.5f?2:0)|(e.v("trans")>.5f?4:0),r=int(e.v("order"));if(m!=maskLast||r!=routeLast){maskLast=m;routeLast=r;order.clear(dontSendNotification);map.clear();StringArray seen;String selected;for(int k=0;k<6;++k){String text;for(int id:routes[k])if(m&(1<<id)){if(text.isNotEmpty())text+=" > ";text+=names[id];}if(text.isEmpty())text="All bypassed";if(k==r)selected=text;if(!seen.contains(text)){seen.add(text);map.push_back(k);order.addItem(text,int(map.size()));}}order.setSelectedItemIndex(seen.indexOf(selected),dontSendNotification);order.setEnabled(map.size()>1);}repaint(710,132,285,42);}
public:
 Editor(Engine& a):AudioProcessorEditor(a),e(a){setLookAndFeel(&look);setSize(1100,490);
 knob("input","INPUT dB",45,12,76,90,Colour(0xff8d3030));knob("output","OUTPUT dB",975,12,76,90,Colour(0xff8d3030));
 addAndMakeVisible(order);order.setBounds(655,47,300,28);order.onChange=[this]{int i=order.getSelectedItemIndex();if(i>=0&&i<int(map.size())){auto* p=e.p.getParameter("order");p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(map[i])));p->endChangeGesture();}};
 toggle("comp","COMP IN",45,116);knob("threshold","THRESHOLD",130,110);knob("attack","ATTACK ms",215,110);knob("release","RELEASE ms",300,110);knob("ratio","RATIO",385,110);knob("makeup","MAKEUP dB",470,110);label("KNEE",555,111,146,18);choices("knee",{"HARD","SOFT"},555,143,73);
 toggle("eq","EQ IN",45,230);StringArray band{"LOW SHELF","LOW MID","MID","HIGH MID","HIGH SHELF"};for(int i=0;i<5;++i){int x=128+i*184;label(band[i],x,211,174,18);knob("freq"+String(i),"Hz",x,237,58,88,Colour(0xff414b58));knob("gain"+String(i),"dB",x+58,237,58,88,Colour(0xff8d3030));if(i>0&&i<4)knob("q"+String(i),"Q",x+116,237,58,88,Colour(0xff414b58));}
 label("HPF Hz",130,330,76,24);choices("hp",{"OFF","30","60","100","150"},210,330,54);label("LPF Hz",506,330,76,24);choices("lp",{"OFF","20k","16k","12k","10k"},584,330,54);
 toggle("trans","TRANS IN",45,406);knob("punch","ATTACK dB",310,385,90,95,Colour(0xff272c2b));knob("sustain","SUSTAIN dB",480,385,90,95,Colour(0xff272c2b));startTimerHz(15);}
 ~Editor()override{stopTimer();setLookAndFeel(nullptr);}
 void paint(Graphics& g)override{g.fillAll(Colour(0xff15181b));auto panel=[&](int y,int h,Colour c){g.setGradientFill(ColourGradient(c.brighter(.14f),0,float(y),c.darker(.22f),0,float(y+h),false));g.fillRoundedRectangle(25.f,float(y),1050.f,float(h),3);g.setColour(Colour(0xffa7a8a4).withAlpha(.4f));g.drawHorizontalLine(y+1,26,1074);g.setColour(Colours::black);g.drawHorizontalLine(y+h-1,26,1074);for(int x:{5,1075}){g.setColour(Colour(0xff34373a));g.fillRect(x,y,20,h);for(int yy:{y+10,y+h-16}){g.setColour(Colours::black);g.fillEllipse(float(x+5),float(yy),10,7);}}};
 panel(3,102,Colour(0xff252b30));panel(108,100,Colour(0xff454a4e));panel(211,155,Colour(0xff343e4c));panel(369,117,Colour(0xff42403a));
 g.setColour(Colour(0xffeee5ce));g.setFont(Font(32.f,Font::bold));g.drawText("TOMINATOR",140,15,430,43,Justification::centredLeft);g.setFont(13.f);g.drawText("DIGITAL TOM PROCESSOR  /  01",142,60,440,19,Justification::centredLeft);g.drawText("SIGNAL ORDER",655,23,300,20,Justification::centredLeft);
 g.drawText("GAIN REDUCTION",710,112,285,18,Justification::centred);float amount=e.gr.load();for(int i=0;i<20;++i){g.setColour(i<amount?Colour(0xffe0ad46):Colour(0xff24282a));g.fillRoundedRectangle(710+i*14.f,140,11,19,2);}g.setColour(Colours::ivory);g.drawText(String(amount,1)+" dB",710,164,285,20,Justification::centred);
 g.setFont(Font(22.f,Font::bold));g.drawText("TRANSIENT DESIGNER",630,401,390,36,Justification::centred);g.setFont(12.f);g.drawText("ATTACK / SUSTAIN  •  CLEAN ENVELOPE CONTROL",630,440,390,20,Justification::centred);
 }
};
AudioProcessorEditor* Engine::createEditor(){return new Editor(*this);}
AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Engine();}
