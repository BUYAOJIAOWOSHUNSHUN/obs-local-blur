/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated Qt/libobs fixture, never loads the user's OBS scene collection. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma warning(push)
#pragma warning(disable:4201)
#include <obs.hpp>
#include <graphics/vec4.h>
#pragma warning(pop)
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>
#include <cstdio>
#include <vector>
#include <QFile>
extern "C" char *load_shader_from_file(const char *);
extern "C" obs_module_t *obs_current_module(void){return nullptr;}
#include "../src/crop-geometry.hpp"
static int checks=0,failures=0,refreshes=0;
static void check(bool ok,const char *name){++checks;printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static void pump(int ms=200){QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();Sleep(2);}}
static bool approx(double a,double b){return std::abs(a-b)<0.0001;}
static crop::Rect getRect(obs_source_t *f){OBSDataAutoRelease s=obs_source_get_settings(f);return crop::fromMargins(obs_data_get_double(s,"effect_mask_crop_left"),obs_data_get_double(s,"effect_mask_crop_top"),obs_data_get_double(s,"effect_mask_crop_right"),obs_data_get_double(s,"effect_mask_crop_bottom"));}
static void setting(obs_source_t *f,const char *key,int value){OBSDataAutoRelease s=obs_data_create();if(strcmp(key,"effect_mask_crop_invert")==0)obs_data_set_bool(s,key,value!=0);else obs_data_set_int(s,key,value);obs_source_update(f,s);pump();}
static void reset(obs_source_t *f){OBSDataAutoRelease s=obs_data_create();obs_data_set_double(s,"effect_mask_crop_left",13.60);obs_data_set_double(s,"effect_mask_crop_top",20.92);obs_data_set_double(s,"effect_mask_crop_right",40.38);obs_data_set_double(s,"effect_mask_crop_bottom",24.06);obs_source_update(f,s);pump();}
struct Fixture{gs_texture_t *texture;uint32_t w,h;};
static void *create(obs_data_t *settings,obs_source_t *)
{
    QImage img(640,360,QImage::Format_RGBA8888);img.fill(QColor("#172534"));
    QPainter p(&img);for(int y=0;y<360;y+=24)for(int x=0;x<640;x+=24)p.fillRect(x,y,24,24,((x+y)/24)%2?QColor("#26485b"):QColor("#35596a"));
    p.setPen(Qt::white);p.setFont(QFont(QStringLiteral("Segoe UI"),24));p.drawText(QRect(30,30,580,50),Qt::AlignCenter,QStringLiteral("LOCAL MASK / OBS 32.2.2"));
    p.setFont(QFont(QStringLiteral("Segoe UI"),16));p.drawText(QRect(40,110,560,140),Qt::AlignCenter|Qt::TextWordWrap,QStringLiteral("Drag inside to move\nDrag handles to resize\nPreview guide stays out of output"));p.end();
    auto *f=new Fixture{};f->w=obs_data_get_int(settings,"fixture_width")>0?uint32_t(obs_data_get_int(settings,"fixture_width")):640;f->h=obs_data_get_int(settings,"fixture_height")>0?uint32_t(obs_data_get_int(settings,"fixture_height")):360;img=img.scaled(int(f->w),int(f->h));const uint8_t *planes[]={img.constBits()};obs_enter_graphics();f->texture=gs_texture_create(f->w,f->h,GS_RGBA,1,planes,0);obs_leave_graphics();return f;
}
static void destroy(void *d){auto *f=static_cast<Fixture*>(d);obs_enter_graphics();gs_texture_destroy(f->texture);obs_leave_graphics();delete f;}
static void render(void *d,gs_effect_t *){auto *f=static_cast<Fixture*>(d);auto *e=obs_get_base_effect(OBS_EFFECT_DEFAULT);gs_effect_set_texture(gs_effect_get_param_by_name(e,"image"),f->texture);while(gs_effect_loop(e,"Draw"))gs_draw_sprite(f->texture,0,f->w,f->h);}
static uint32_t width(void *d){return static_cast<Fixture*>(d)->w;}static uint32_t height(void *d){return static_cast<Fixture*>(d)->h;}static const char *name(void *){return "Fixture";}
static QImage capture(obs_source_t *s)
{
    QImage img(640,360,QImage::Format_RGBA8888);img.fill(Qt::transparent);obs_enter_graphics();
    auto *target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto *stage=gs_stagesurface_create(640,360,GS_RGBA);
    if(gs_texrender_begin(target,640,360)){
        vec4 c;vec4_zero(&c);gs_clear(GS_CLEAR_COLOR,&c,0,0);gs_ortho(0,640,0,360,-100,100);obs_source_video_render(s);gs_texrender_end(target);
        gs_stage_texture(stage,gs_texrender_get_texture(target));uint8_t *mapped=nullptr;uint32_t stride=0;
        if(gs_stagesurface_map(stage,&mapped,&stride)){for(int y=0;y<360;y++)memcpy(img.scanLine(y),mapped+y*stride,640*4);gs_stagesurface_unmap(stage);}else check(false,"GPU readback");
    }else check(false,"GPU render target");
    gs_stagesurface_destroy(stage);gs_texrender_destroy(target);obs_leave_graphics();return img;
}
// GPU timestamps exclude CPU scheduling and texture readback time.
static void benchmark()
{
    for(int benchWidth:{640,1920}) {
        const int benchHeight=benchWidth*9/16;
        OBSDataAutoRelease sourceSettings=obs_data_create();obs_data_set_int(sourceSettings,"fixture_width",benchWidth);obs_data_set_int(sourceSettings,"fixture_height",benchHeight);
        auto *source=obs_source_create("crop_test_fixture","Performance fixture",sourceSettings,nullptr);
        OBSDataAutoRelease settings=obs_data_create();obs_data_set_int(settings,"effect_mask",1);obs_data_set_double(settings,"radius",12);obs_data_set_double(settings,"pixelate_size",12);obs_data_set_double(settings,"effect_mask_crop_left",25);obs_data_set_double(settings,"effect_mask_crop_right",25);obs_data_set_double(settings,"effect_mask_crop_top",25);obs_data_set_double(settings,"effect_mask_crop_bottom",25);
        auto *filter=obs_source_create("obs_composite_blur","Benchmark filter",settings,nullptr);obs_source_filter_add(source,filter);obs_source_inc_showing(source);
        obs_enter_graphics();auto *target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);auto *stage=gs_stagesurface_create(benchWidth,benchHeight,GS_RGBA);auto *timer=gs_timer_create();auto *range=gs_timer_range_create();obs_leave_graphics();
        for(int mode:{0,4,1,3}) {
            obs_source_set_enabled(filter,mode!=0);obs_data_set_int(settings,"blur_algorithm",mode?mode:4);obs_source_update(filter,settings);pump(200);
            std::vector<double> samples;
            for(int frame=0;frame<28;frame++) {
                pump(40);obs_enter_graphics();gs_texrender_reset(target);gs_timer_range_begin(range);gs_timer_begin(timer);
                if(gs_texrender_begin(target,benchWidth,benchHeight)) {vec4 clear;vec4_zero(&clear);gs_clear(GS_CLEAR_COLOR,&clear,0,0);gs_ortho(0,float(benchWidth),0,float(benchHeight),-100,100);obs_source_video_render(source);gs_texrender_end(target);}
                gs_timer_end(timer);gs_timer_range_end(range);gs_stage_texture(stage,gs_texrender_get_texture(target));uint8_t *map=nullptr;uint32_t stride=0;if(gs_stagesurface_map(stage,&map,&stride))gs_stagesurface_unmap(stage);
                uint64_t ticks=0,frequency=0;bool disjoint=true;
                const bool ok=gs_timer_get_data(timer,&ticks)&&gs_timer_range_get_data(range,&disjoint,&frequency)&&!disjoint&&frequency;
                if(ok&&frame>=4)samples.push_back(double(ticks)*1000/double(frequency));obs_leave_graphics();
            }
            if(samples.size()<20){check(false,"sufficient GPU timing samples");}else{std::sort(samples.begin(),samples.end());printf("PERF %dx%d mode=%d samples=%zu median_ms=%.4f p95_ms=%.4f\n",benchWidth,benchHeight,mode,samples.size(),samples[samples.size()/2],samples[size_t((samples.size()-1)*.95)]);}
        }
        obs_enter_graphics();gs_timer_destroy(timer);gs_timer_range_destroy(range);gs_stagesurface_destroy(stage);gs_texrender_destroy(target);obs_leave_graphics();obs_source_dec_showing(source);obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);
    }
}
static crop::Rect viewport(QWidget *w){return crop::fit(640,360,w->width(),w->height());}
static QPointF pixel(QWidget *w,double x,double y){auto v=viewport(w);return {v.left+x*(v.right-v.left),v.top+y*(v.bottom-v.top)};}
static void mouse(QWidget *w,QEvent::Type type,QPointF pos,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{QMouseEvent e(type,pos,w->mapToGlobal(pos),button,buttons,modifiers);QApplication::sendEvent(w,&e);pump(15);}
static void drag(QWidget *w,QPointF start,QPointF delta,bool cancel=false,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    mouse(w,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(w,QEvent::MouseMove,start+delta,Qt::NoButton,Qt::LeftButton,modifiers);
    if(cancel){QKeyEvent e(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(w,&e);}else mouse(w,QEvent::MouseButtonRelease,start+delta,Qt::LeftButton,Qt::NoButton);
    pump();
}
static void refreshed(void *,calldata_t *){++refreshes;}
int wmain(int argc,wchar_t **wide)
{
    std::vector<QByteArray> values;std::vector<char*> pointers;
    for(int i=0;i<argc;i++)values.push_back(QString::fromWCharArray(wide[i]).toUtf8());
    for(auto &value:values)pointers.push_back(value.data());pointers.push_back(nullptr);char **argv=pointers.data();
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);setvbuf(stdout,nullptr,_IONBF,0);
    QApplication app(argc,argv);if(argc!=5)return 2;
    check(obs_startup("zh-CN",nullptr,nullptr),"libobs startup");
    auto shaderCase=[&](const char *name,const QByteArray &contents,bool expected){
        const QString path=QString::fromUtf8(argv[4])+"/"+QString::fromUtf8(name);
        QFile f(path);if(!f.open(QIODevice::WriteOnly))return false;f.write(contents);f.close();
        char *loaded=load_shader_from_file(path.toUtf8().constData());bool ok=(loaded!=nullptr)==expected;bfree(loaded);return ok;};
    check(shaderCase("valid.effect","float value;\n",true),"shader loader accepts valid file");
    check(shaderCase("include.effect","#include \"valid.effect\"\n",true),"shader loader resolves relative include");
    check(shaderCase("bad.effect","#include missing_quotes\n",false),"malformed shader include fails cleanly");
    check(shaderCase("missing.effect","#include \"not-present.effect\"\n",false),"missing shader include fails cleanly");
    check(shaderCase("cycle.effect","#include \"cycle.effect\"\n",false),"cyclic shader include is bounded");
    check(shaderCase("large.effect",QByteArray(1024*1024+1,' '),false),"oversized shader is rejected before unbounded allocation");
#pragma warning(push)
#pragma warning(disable:4996)
    obs_add_data_path(argv[1]);
#pragma warning(pop)
    const auto graphics=QString::fromUtf8(argv[1])+QStringLiteral("/../../bin/64bit/libobs-d3d11.dll");const auto g=graphics.toUtf8();
    obs_video_info vi={};vi.graphics_module=g.constData();vi.fps_num=30;vi.fps_den=1;vi.base_width=640;vi.base_height=360;vi.output_width=640;vi.output_height=360;vi.output_format=VIDEO_FORMAT_RGBA;vi.colorspace=VIDEO_CS_709;vi.range=VIDEO_RANGE_FULL;vi.scale_type=OBS_SCALE_BILINEAR;
    const int videoResult=obs_reset_video(&vi);check(videoResult==0,"D3D11 runtime");if(videoResult)return 3;
    obs_module_t *module=nullptr;const int open=obs_open_module(&module,argv[2],argv[3]);check(open==0&&module,"modified plugin opens");if(open)return 4;check(obs_init_module(module),"modified plugin initializes");
    check(strcmp(obs_source_get_display_name("obs_composite_blur"),"局部打码")==0,"Chinese effect-filter display name");
    auto dll=LoadLibraryW(QString::fromUtf8(argv[2]).toStdWString().c_str());
    auto start=reinterpret_cast<void(*)()>(GetProcAddress(dll,"cb_crop_ui_start"));auto stop=reinterpret_cast<void(*)()>(GetProcAddress(dll,"cb_crop_ui_stop"));check(start&&stop,"UI lifecycle exports");
    obs_source_info info={};info.id="crop_test_fixture";info.type=OBS_SOURCE_TYPE_INPUT;info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;info.get_name=name;info.create=create;info.destroy=destroy;info.get_width=width;info.get_height=height;info.video_render=render;obs_register_source(&info);
    if(qEnvironmentVariableIsSet("CB_PERF_ONLY")){benchmark();obs_wait_for_destroy_queue();obs_shutdown();FreeLibrary(dll);return failures?1:0;}
    obs_source_t *source=obs_source_create(info.id,"Synthetic test source",nullptr,nullptr);
    OBSDataAutoRelease settings=obs_data_create();obs_data_set_int(settings,"effect_mask",1);obs_data_set_int(settings,"blur_algorithm",4);obs_data_set_double(settings,"radius",12);obs_data_set_double(settings,"pixelate_size",12);
    obs_source_t *filter=obs_source_create("obs_composite_blur","Composite Blur",settings,nullptr);obs_source_filter_add(source,filter);reset(filter);obs_source_inc_showing(source);
    auto *actualProperties=obs_source_properties(filter);
    auto *modes=obs_properties_get(actualProperties,"effect_mask");
    check(obs_property_list_item_count(modes)==3&&obs_property_list_item_int(modes,0)==0&&obs_property_list_item_int(modes,1)==1&&obs_property_list_item_int(modes,2)==3,"only none, crop and circle-ellipse mask modes exposed");
    check(!obs_properties_get(actualProperties,"background")&&!obs_properties_get(actualProperties,"effect_mask_rect")&&!obs_properties_get(actualProperties,"effect_mask_source"),"background composition and removed mask parameter groups absent");
    {OBSDataAutoRelease defaults=obs_get_source_defaults("obs_composite_blur");check(obs_data_get_int(defaults,"effect_mask")==1,"new filters default to crop mask");}
    check(QString::fromUtf8(obs_property_list_item_name(modes,1))==QStringLiteral("矩形"),"crop mode displayed as rectangle in Chinese");
    std::vector<std::string> propertyOrder;for(auto *property=obs_properties_first(actualProperties);property;obs_property_next(&property))propertyOrder.emplace_back(obs_property_name(property));
    auto orderOf=[&](const char *key){return std::find(propertyOrder.begin(),propertyOrder.end(),key)-propertyOrder.begin();};
    check(propertyOrder.front()=="cb_mask_section"&&orderOf("cb_mask_section")<orderOf("effect_mask")&&orderOf("effect_mask_circle")<orderOf("cb_effect_section")&&orderOf("effect_mask_crop")<orderOf("cb_effect_section")&&orderOf("cb_effect_section")<orderOf("blur_algorithm"),"all mask controls precede separated blur and pixelation controls");
    auto *pluginInfo=obs_properties_get(actualProperties,"plugin_info");
    const QString credit=QString::fromUtf8(obs_property_description(pluginInfo));
    check(credit.contains(QStringLiteral("https://github.com/BUYAOJIAOWOSHUNSHUN/obs-local-blur"))&&credit.contains(QStringLiteral("https://space.bilibili.com/40900884"))&&credit.contains(QStringLiteral("https://github.com/finitesingularity/obs-composite-blur/")),"credit links point to plugin repository, Bilibili author and upstream");
    check(credit.count(QStringLiteral("text-decoration:underline"))==3&&credit.contains(QStringLiteral("margin:0 0 6px 0"))&&credit.contains(QStringLiteral("Composite Blur 1.6.1</a>")),"two credit paragraphs with spacing and three blue underlined links");
    QLabel creditPreview;creditPreview.setTextFormat(Qt::RichText);creditPreview.setText(credit);
    creditPreview.setStyleSheet("QLabel {background:#242731;color:#eff2f6;padding:16px;font-size:14px;}");
    creditPreview.setFixedWidth(620);creditPreview.adjustSize();
    creditPreview.grab().save(QString::fromUtf8(argv[4])+"/credit-preview.png");
    const QString maskTitle=QString::fromUtf8(obs_property_description(obs_properties_get(actualProperties,"cb_mask_section")));
    const QString styleTitle=QString::fromUtf8(obs_property_description(obs_properties_get(actualProperties,"cb_effect_section")));
    check(maskTitle==QStringLiteral("模糊区域参数")&&styleTitle==QStringLiteral("马赛克样式参数"),"section titles are exact plain text without HTML markup");
    check(QString::fromUtf8(obs_property_description(obs_properties_get(actualProperties,"effect_mask_crop_invert")))==QStringLiteral("反转蒙版"),"invert mask label has no question mark");
    obs_properties_destroy(actualProperties);
    // A missing long UTF-8 filename must still be retained without overwriting
    // a fixed allocation. Exercise longer/shorter/repeated updates and cleanup.
    for(int length:{254,255,256,1024,4,0}) {
        const QByteArray path=QString(length,QChar(0x6D4B)).toUtf8();
        OBSDataAutoRelease pathSettings=obs_data_create();
        obs_data_set_string(pathSettings,"effect_mask_source_file",path.constData());
        obs_source_update(filter,pathSettings);obs_source_update(filter,pathSettings);
        OBSDataAutoRelease current=obs_source_get_settings(filter);
        check(QByteArray(obs_data_get_string(current,"effect_mask_source_file"))==path,"long UTF-8 mask path updates preserve full settings");
    }
    signal_handler_connect(obs_source_get_signal_handler(filter),"update_properties",refreshed,nullptr);
    check(strcmp(obs_source_get_name(filter),"Composite Blur")==0,"existing filter-instance name preserved");
    OBSDataAutoRelease sharedPanelSettings=obs_source_get_settings(filter);
    const auto before=capture(source);before.save(QString::fromUtf8(argv[4])+"/output-before.png");
    auto *dialog=new QDialog();dialog->setObjectName("OBSBasicFilters");dialog->setWindowTitle(QStringLiteral("局部打码 · 独立预览测试（未安装）"));dialog->resize(900,700);
    dialog->setStyleSheet("QDialog,QFrame,QGroupBox {background:#242731;color:#eff2f6;} QLabel{color:#eff2f6;} QGroupBox{border:1px solid #444b5d;margin-top:12px;padding:10px;}");
    auto *outer=new QVBoxLayout(dialog);auto *frame=new QFrame(dialog);frame->setObjectName("previewFrame");outer->addWidget(frame,1);auto *layout=new QVBoxLayout(frame);layout->setContentsMargins(0,0,0,0);
    auto *original=new QWidget(frame);original->setObjectName("preview");original->setMinimumHeight(220);layout->addWidget(original);
    auto *properties=new QFrame(dialog);properties->setObjectName("propertiesFrame");outer->addWidget(properties);auto *propertyLayout=new QVBoxLayout(properties);
    auto *headerHost=new QWidget(properties);auto *headerForm=new QFormLayout(headerHost);auto *maskHeader=new QLabel(maskTitle,headerHost);auto *styleHeader=new QLabel(styleTitle,headerHost);headerForm->addRow(static_cast<QWidget*>(nullptr),maskHeader);headerForm->addRow(static_cast<QWidget*>(nullptr),styleHeader);propertyLayout->addWidget(headerHost);
    auto *group=new QGroupBox(QStringLiteral("裁剪蒙版参数"),properties);propertyLayout->addWidget(group);auto *form=new QFormLayout(group);
    auto *marker=new QLabel(QStringLiteral("<span><!--cb-crop-editor:%1--></span>").arg(QString::fromUtf8(obs_source_get_uuid(filter))),group);form->addRow(marker);
    for(const auto *text:{"顶部 (%)","底部 (%)","左侧 (%)","右侧 (%)"})form->addRow(QString::fromUtf8(text),new QLabel("20.00",group));
    form->addRow(QStringLiteral("圆角半径 (%)"),new QLabel("0.00",group));form->addRow(QStringLiteral("羽化 (%)"),new QLabel("0.00",group));form->addRow(new QLabel(QStringLiteral("反转裁剪：未勾选"),group));
    dialog->show();start();pump(500);
    auto *preview=dialog->findChild<QWidget*>("cbVisualCropPreview");check(preview&&preview->isVisible(),"interactive preview inserted in original preview position");
    if(!preview){stop();delete dialog;obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_shutdown();return 5;}
    {int headerRow=-1;QFormLayout::ItemRole headerRole;headerForm->getWidgetPosition(maskHeader,&headerRow,&headerRole);check(headerRole==QFormLayout::SpanningRole&&maskHeader->alignment().testFlag(Qt::AlignLeft),"mask heading spans form columns and aligns left");headerForm->getWidgetPosition(styleHeader,&headerRow,&headerRole);check(headerRole==QFormLayout::SpanningRole&&styleHeader->alignment().testFlag(Qt::AlignLeft),"style heading spans form columns and aligns left");}
    check(maskHeader->textFormat()==Qt::PlainText&&styleHeader->textFormat()==Qt::PlainText&&maskHeader->font().bold()&&styleHeader->font().bold(),"native title labels use plain text and bold font");
    headerHost->grab().save(QString::fromUtf8(argv[4])+"/plain-titles.png");
    check(original->isHidden(),"original preview kept alive and hidden");check(!form->isRowVisible(1)&&!form->isRowVisible(4),"four margin rows integrated into preview");check(form->isRowVisible(5)&&form->isRowVisible(6),"corner radius and feather controls retained");
    check(preview->toolTip().isEmpty(),"preview instruction tooltip removed");
    check(strcmp(obs_source_get_name(filter),"局部打码")==0,"old exact default name migrated in effect-filter list");
    auto v=viewport(preview);check(v.top>0||v.left>0,"letterbox coordinate mapping exercised");
    const int refreshesBeforeDragging=refreshes;
    auto r=getRect(filter);drag(preview,pixel(preview,(r.left+r.right)/2,(r.top+r.bottom)/2),{30,15});auto moved=getRect(filter);
    check(approx(moved.left,r.left+30/(v.right-v.left))&&approx(moved.top,r.top+15/(v.bottom-v.top)),"drag moves mask in real time");check(approx(moved.right-moved.left,r.right-r.left)&&approx(moved.bottom-moved.top,r.bottom-r.top),"move preserves mask dimensions");
    r=moved;drag(preview,pixel(preview,r.right,(r.top+r.bottom)/2),{25,0});auto resized=getRect(filter);check(approx(resized.right,r.right+25/(v.right-v.left))&&approx(resized.left,r.left),"right-edge handle resizes mask");
    r=resized;drag(preview,pixel(preview,r.left,r.top),{-20,-10});auto corner=getRect(filter);check(corner.left<r.left&&corner.top<r.top&&approx(corner.right,r.right)&&approx(corner.bottom,r.bottom),"corner handle adjusts two margins");
    r=corner;drag(preview,pixel(preview,(r.left+r.right)/2,(r.top+r.bottom)/2),{9999,9999});auto clamped=getRect(filter);check(approx(clamped.right,1)&&approx(clamped.bottom,1)&&crop::valid(clamped),"drag clamps at source boundary");
    r=clamped;drag(preview,pixel(preview,(r.left+r.right)/2,(r.top+r.bottom)/2),{-40,-20},true);auto canceled=getRect(filter);check(approx(canceled.left,r.left)&&approx(canceled.top,r.top),"Escape cancels current gesture");check(refreshes==refreshesBeforeDragging,"dragging does not rebuild or jump the properties panel");
    const auto settled=getRect(filter);obs_data_set_double(sharedPanelSettings,"effect_mask_crop_feathering",5);obs_source_update(filter,sharedPanelSettings);pump();
    const auto afterFeather=getRect(filter);check(approx(settled.left,afterFeather.left)&&approx(settled.top,afterFeather.top)&&approx(settled.right,afterFeather.right)&&approx(settled.bottom,afterFeather.bottom),"editing feather through retained panel settings preserves dragged bounds");
    obs_data_set_double(sharedPanelSettings,"effect_mask_crop_feathering",0);obs_source_update(filter,sharedPanelSettings);pump();
    reset(filter);pump();auto after=capture(source);after.save(QString::fromUtf8(argv[4])+"/output-with-editor.png");check(before==after,"editor frame and handles never enter source output");
    using Snapshot=bool(*)(void*,const char*);
    auto snapshot=reinterpret_cast<Snapshot>(GetProcAddress(dll,"cb_crop_ui_capture_preview"));
    const auto previewPath=(QString::fromUtf8(argv[4])+"/visual-crop-preview.png").toUtf8();
    check(snapshot&&snapshot(preview,previewPath.constData()),"private preview GPU readback");
    QImage pixels(QString::fromUtf8(previewPath));int cyan=0;for(int y=0;y<pixels.height();y++)for(int x=0;x<pixels.width();x++){const auto c=pixels.pixelColor(x,y);if(c.green()>160&&c.blue()>200&&c.red()<100)++cyan;}
    check(cyan>100,"private preview draw callback renders cyan crop border");
    auto countCyan=[](const QImage &img,QRect area){int count=0;area=area.intersected(img.rect());for(int y=area.top();y<=area.bottom();y++)for(int x=area.left();x<=area.right();x++){auto c=img.pixelColor(x,y);if(c.green()>160&&c.blue()>200&&c.red()<100)++count;}return count;};
    const auto pv=crop::fit(640,360,pixels.width(),pixels.height());const auto pr=getRect(filter);
    const double px=pv.left+pr.left*(pv.right-pv.left),py=pv.top+pr.top*(pv.bottom-pv.top);
    const double pw=(pr.right-pr.left)*(pv.right-pv.left),ph=(pr.bottom-pr.top)*(pv.bottom-pv.top);
    const int sampleX=int(px+pw/4),top=int(py),thickness=int(std::ceil(preview->devicePixelRatioF()));
    check(countCyan(pixels,QRect(sampleX,top-4,1,12))<=thickness+1,"border uses one logical pixel between handles");
    bool midpointHandles=true;const crop::Point mids[]={{px+pw/2,py},{px+pw,py+ph/2},{px+pw/2,py+ph},{px,py+ph/2}};
    for(const auto &p:mids){auto c=pixels.pixelColor(int(p.x),int(p.y));midpointHandles=midpointHandles&&c.red()>220&&c.green()>230&&c.blue()>240;}
    check(midpointHandles,"all four white edge-midpoint handles retained");
    int whiteHandles=0;for(int y=std::max(0,top-10);y<std::min(pixels.height(),top+10);y++)for(int x=std::max(0,int(px)-10);x<std::min(pixels.width(),int(px)+10);x++){auto c=pixels.pixelColor(x,y);if(c.red()>220&&c.green()>230&&c.blue()>240)++whiteHandles;}
    check(whiteHandles==0,"white corner blocks removed");
    setting(filter,"effect_mask_crop_corner_radius",35);
    const auto roundedPath=(QString::fromUtf8(argv[4])+"/rounded-preview.png").toUtf8();
    check(snapshot(preview,roundedPath.constData()),"rounded preview GPU readback");
    QImage rounded(QString::fromUtf8(roundedPath));const double rad=.35*std::min(pw,ph);
    const double arcX=px+rad-rad/std::sqrt(2.0),arcY=py+rad-rad/std::sqrt(2.0);
    check(countCyan(rounded,QRect(int(px)-2,int(py)-2,8,8))==0&&countCyan(rounded,QRect(int(arcX)-3,int(arcY)-3,7,7))>0,"live rounded border follows crop-relative radius at 35 percent");
    setting(filter,"effect_mask_crop_corner_radius",50);
    const auto capsulePath=(QString::fromUtf8(argv[4])+"/rounded-50-preview.png").toUtf8();snapshot(preview,capsulePath.constData());
    check(QImage(QString::fromUtf8(capsulePath))!=rounded,"radius slider changes preview without reopening editor");
    const auto roundedOutput=capture(source);stop();pump();
    check(roundedOutput==capture(source),"rounded guide never enters source output");
    start();pump();preview=dialog->findChild<QWidget*>("cbVisualCropPreview");
    setting(filter,"effect_mask_crop_corner_radius",0);
    const auto squarePath=(QString::fromUtf8(argv[4])+"/restored-square-preview.png").toUtf8();snapshot(preview,squarePath.constData());
    check(QImage(QString::fromUtf8(squarePath))==pixels,"zero radius restores original thin rectangular border");
    setting(filter,"effect_mask_crop_invert",1);pump();auto *hint=dialog->findChild<QLabel*>("cbCropEditorHint");check(hint&&!hint->isVisible()&&hint->text().isEmpty(),"normal crop editing has no instruction line or reserved space");
    setting(filter,"effect_mask",0);check(original->isVisible()&&!preview->isVisible(),"other mask modes restore original preview");check(layout->indexOf(original)>=0&&form->isRowVisible(1),"original preview layout and numeric fallback restored");
    setting(filter,"effect_mask",1);check(preview->isVisible()&&layout->indexOf(preview)>=0,"crop editor reattaches when returning to crop mode");
    // Settings from an older scene can encode an empty/inverted rectangle.
    {OBSDataAutoRelease invalid=obs_data_create();obs_data_set_double(invalid,"effect_mask_crop_left",80);obs_data_set_double(invalid,"effect_mask_crop_right",80);obs_source_update(filter,invalid);}pump();check(form->isRowVisible(1)&&hint->text().contains(QStringLiteral("为空")),"invalid old crop settings retain numeric repair controls");reset(filter);pump();
    const auto x=crop::drag({0.2,0.2,0.7,0.8},crop::Left,{2,0},0.01,0.01);check(crop::valid(x)&&approx(x.right-x.left,0.01),"resize cannot cross opposite edge");
    const auto letterbox=crop::fit(1920,1080,1000,1000);check(approx(letterbox.top,218.75)&&approx(letterbox.bottom,781.25),"aspect ratio mapping across square preview");
    const int handles[]={crop::Left,crop::Right,crop::Top,crop::Bottom,crop::Left|crop::Top,crop::Right|crop::Top,crop::Left|crop::Bottom,crop::Right|crop::Bottom};
    for(int handle:handles) {
        const auto resizedAll=crop::drag({.2,.2,.8,.8},handle,{.05,.05},.01,.01);
        check(crop::valid(resizedAll),"each edge and corner handle remains valid");
    }
    // Ellipse geometry is stored using source percentages and shorter-side radii.
    auto ellipseRect=[&](){OBSDataAutoRelease d=obs_source_get_settings(filter);double x=obs_data_get_double(d,"effect_mask_circle_center_x")/100,y=obs_data_get_double(d,"effect_mask_circle_center_y")/100,rx=obs_data_get_double(d,"effect_mask_circle_radius")/100*360/640,ry=obs_data_has_user_value(d,"effect_mask_circle_radius_y")?obs_data_get_double(d,"effect_mask_circle_radius_y")/100:obs_data_get_double(d,"effect_mask_circle_radius")/100;return crop::Rect{x-rx,y-ry,x+rx,y+ry};};
    setting(filter,"effect_mask",3);setting(filter,"effect_mask_circle_radius",20);setting(filter,"effect_mask_circle_radius_y",12);
    check(preview->isVisible()&&!form->isRowVisible(1)&&!form->isRowVisible(4),"ellipse uses same interactive preview with numeric fields integrated");
    obs_source_set_enabled(filter,false);pump();const auto cleanOutput=capture(source);obs_source_set_enabled(filter,true);pump();
    const auto ellipseOutput=capture(source);int insideChanged=0,outsideChanged=0;
    for(int ellipsePixelY=0;ellipsePixelY<360;ellipsePixelY++)for(int ellipsePixelX=0;ellipsePixelX<640;ellipsePixelX++){
        const double dx=(ellipsePixelX+.5-320)/72,dy=(ellipsePixelY+.5-180)/43.2,d=dx*dx+dy*dy;
        if(ellipseOutput.pixel(ellipsePixelX,ellipsePixelY)!=cleanOutput.pixel(ellipsePixelX,ellipsePixelY)){if(d<.9)insideChanged++;if(d>1.1)outsideChanged++;}
    }
    check(insideChanged>50&&outsideChanged==0,"actual GPU effect follows elliptical bounds, with untouched exterior");
    auto oval=ellipseRect();v=viewport(preview);
    drag(preview,pixel(preview,.5,.5),{20,10});auto ovalMoved=ellipseRect();
    check(approx(ovalMoved.left,oval.left+20/(v.right-v.left))&&approx(ovalMoved.top,oval.top+10/(v.bottom-v.top)),"ellipse center handle moves its real coordinates");
    drag(preview,pixel(preview,ovalMoved.right,(ovalMoved.top+ovalMoved.bottom)/2),{24,0});auto ovalWide=ellipseRect();
    check(ovalWide.right>ovalMoved.right&&approx(ovalWide.left,ovalMoved.left)&&approx(ovalWide.top,ovalMoved.top)&&approx(ovalWide.bottom,ovalMoved.bottom),"right midpoint changes ellipse width only");
    drag(preview,pixel(preview,(ovalWide.left+ovalWide.right)/2,ovalWide.bottom),{0,18});auto ovalTall=ellipseRect();
    check(ovalTall.bottom>ovalWide.bottom&&approx(ovalTall.left,ovalWide.left)&&approx(ovalTall.right,ovalWide.right),"bottom midpoint changes ellipse height only");
    const double ratioOval=(ovalTall.right-ovalTall.left)/(ovalTall.bottom-ovalTall.top);
    drag(preview,pixel(preview,ovalTall.right,ovalTall.bottom),{18,14});auto ovalScaled=ellipseRect();
    check(approx((ovalScaled.right-ovalScaled.left)/(ovalScaled.bottom-ovalScaled.top),ratioOval),"ellipse corner scales both axes while preserving aspect ratio");
    drag(preview,pixel(preview,ovalScaled.right,(ovalScaled.top+ovalScaled.bottom)/2),{12,0},false,Qt::ShiftModifier);auto ovalShift=ellipseRect();
    check(approx((ovalShift.right-ovalShift.left)/(ovalShift.bottom-ovalShift.top),ratioOval),"Shift midpoint drag locks ellipse aspect ratio");
    drag(preview,pixel(preview,(ovalShift.left+ovalShift.right)/2,(ovalShift.top+ovalShift.bottom)/2),{40,20},true);auto ovalCanceled=ellipseRect();
    check(approx(ovalCanceled.left,ovalShift.left)&&approx(ovalCanceled.top,ovalShift.top)&&approx(ovalCanceled.right,ovalShift.right),"Escape restores ellipse move without changing its axes");
    const auto ovalPath=(QString::fromUtf8(argv[4])+"/ellipse-preview.png").toUtf8();check(snapshot(preview,ovalPath.constData()),"ellipse preview GPU readback");
    QImage ovalPixels(QString::fromUtf8(ovalPath));const auto ov=crop::fit(640,360,ovalPixels.width(),ovalPixels.height());int handlesWhite=0;
    for(double ny:{ovalShift.top,(ovalShift.top+ovalShift.bottom)/2,ovalShift.bottom})for(double nx:{ovalShift.left,(ovalShift.left+ovalShift.right)/2,ovalShift.right}){auto c=ovalPixels.pixelColor(int(ov.left+nx*(ov.right-ov.left)),int(ov.top+ny*(ov.bottom-ov.top)));if(c.red()>220&&c.green()>230&&c.blue()>240)handlesWhite++;}
    check(handlesWhite==9,"ellipse preview has exactly nine expected white control points");
    auto withEllipseEditor=capture(source);stop();pump();check(withEllipseEditor==capture(source),"ellipse guide and nine handles never enter source output");start();pump();preview=dialog->findChild<QWidget*>("cbVisualCropPreview");
    const auto beforeReset=ellipseRect();
    auto *resetProperties=obs_source_properties(filter);auto *resetButton=obs_properties_get(resetProperties,"effect_mask_circle_reset");
    check(resetButton&&QString::fromUtf8(obs_property_description(resetButton))==QStringLiteral("恢复正圆"),"circle reset button exposed in mask group");
    if(resetButton)obs_property_button_clicked(resetButton,nullptr);obs_properties_destroy(resetProperties);pump();
    const auto afterReset=ellipseRect();
    check(approx((afterReset.right-afterReset.left)*640,(afterReset.bottom-afterReset.top)*360),"reset button restores equal pixel radii");
    check(approx(afterReset.left+afterReset.right,beforeReset.left+beforeReset.right)&&approx(afterReset.top+afterReset.bottom,beforeReset.top+beforeReset.bottom),"circle reset preserves mask center");
    {OBSDataAutoRelease d=obs_source_get_settings(filter);obs_data_unset_user_value(d,"effect_mask_circle_radius_y");obs_data_set_double(d,"effect_mask_circle_radius",15);obs_source_update(filter,d);}pump();
    auto legacyCircle=ellipseRect();check(approx((legacyCircle.right-legacyCircle.left)*640,(legacyCircle.bottom-legacyCircle.top)*360),"old single-radius settings remain a true circle");
    // Real D3D11 rendering on a tiny source exercises the downsample floor.
    {OBSDataAutoRelease tinySettings=obs_data_create();obs_data_set_int(tinySettings,"fixture_width",3);obs_data_set_int(tinySettings,"fixture_height",1);
    auto *tiny=obs_source_create(info.id,"Tiny regression fixture",tinySettings,nullptr);
    OBSDataAutoRelease blurSettings=obs_data_create();obs_data_set_int(blurSettings,"blur_algorithm",3);obs_data_set_int(blurSettings,"effect_mask",0);obs_data_set_double(blurSettings,"kawase_passes",1025);
    auto *tinyFilter=obs_source_create("obs_composite_blur","Tiny blur",blurSettings,nullptr);obs_source_filter_add(tiny,tinyFilter);obs_source_inc_showing(tiny);pump();
    auto tinyOutput=capture(tiny);check(tinyOutput.pixelColor(0,0).alpha()>0,"one-pixel-high source survives maximum multilevel blur");
    obs_source_dec_showing(tiny);obs_source_filter_remove(tiny,tinyFilter);obs_source_release(tinyFilter);obs_source_release(tiny);}
    setting(filter,"effect_mask",1);reset(filter);
    OBSDataAutoRelease secondSettings=obs_data_create();obs_data_apply(secondSettings,settings);
    obs_source_t *second=obs_source_create("obs_composite_blur","马赛克",secondSettings,nullptr);obs_source_filter_add(source,second);reset(second);
    const auto firstRect=getRect(filter);marker->setText(QStringLiteral("<span><!--cb-crop-editor:%1--></span>").arg(QString::fromUtf8(obs_source_get_uuid(second))));pump();
    r=getRect(second);drag(preview,pixel(preview,(r.left+r.right)/2,(r.top+r.bottom)/2),{12,6});auto other=getRect(second);
    printf("FILTER_SWITCH first %.6f -> %.6f; second %.6f -> %.6f; previewVisible %d\n",firstRect.left,getRect(filter).left,r.left,other.left,int(preview->isVisible()));
    check(!approx(other.left,r.left)&&approx(getRect(filter).left,firstRect.left),"switching filter UUID edits only selected filter");
    check(strcmp(obs_source_get_name(second),"马赛克")==0,"custom effect-filter name preserved");
    dialog->close();pump();check(!dialog->findChild<QWidget*>("cbVisualCropPreview"),"close releases private display safely");delete dialog;pump();stop();
    signal_handler_disconnect(obs_source_get_signal_handler(filter),"update_properties",refreshed,nullptr);
    obs_source_dec_showing(source);obs_source_filter_remove(source,second);obs_source_release(second);obs_source_filter_remove(source,filter);obs_source_release(filter);obs_source_release(source);obs_wait_for_destroy_queue();obs_shutdown();FreeLibrary(dll);
    printf("RESULT %d checks, %d failures\n",checks,failures);return failures?1:0;
}
