/* SPDX-License-Identifier: GPL-2.0-or-later
 * Local visual-crop adaptation of Composite Blur 1.6.1 for OBS 32.2.2.
 * Owns a separate libobs display in the existing preview position. No private
 * OBS C++ object layouts and no overlay is inserted into the source render. */
#include "crop-ui.h"
#include "crop-geometry.hpp"
#include <obs.hpp>
#include <QApplication>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <mutex>

namespace {
constexpr const char *filterId="obs_composite_blur";
const QString markerPrefix=QStringLiteral("cb-crop-editor:");
crop::Rect readCrop(obs_source_t *filter)
{
    OBSDataAutoRelease s=obs_source_get_settings(filter);
    return crop::fromMargins(obs_data_get_double(s,"effect_mask_crop_left"),obs_data_get_double(s,"effect_mask_crop_top"),
        obs_data_get_double(s,"effect_mask_crop_right"),obs_data_get_double(s,"effect_mask_crop_bottom"));
}
void writeCrop(obs_source_t *filter,crop::Rect r)
{
    OBSDataAutoRelease s=obs_data_create();
    obs_data_set_double(s,"effect_mask_crop_left",r.left*100);
    obs_data_set_double(s,"effect_mask_crop_top",r.top*100);
    obs_data_set_double(s,"effect_mask_crop_right",(1-r.right)*100);
    obs_data_set_double(s,"effect_mask_crop_bottom",(1-r.bottom)*100);
    obs_source_update(filter,s);
}
bool isCrop(obs_source_t *filter)
{
    if(!filter||obs_source_removed(filter)||strcmp(obs_source_get_unversioned_id(filter),filterId)!=0) return false;
    OBSDataAutoRelease s=obs_source_get_settings(filter);
    return obs_data_get_int(s,"effect_mask")==1;
}

class CropPreview final:public QWidget {
    obs_display_t *display=nullptr;
    std::mutex stateMutex;
    OBSWeakSource weakFilter,weakParent;
    bool dragging=false;
    int activeHandle=crop::None;
    crop::Point pressPoint={0,0};
    crop::Rect startRect={0,0,1,1};
    OBSWeakSource gestureFilter;
    bool closing=false;
    double pixelRatio=1;
    static void quad(double x,double y,double w,double h,uint32_t color)
    {
        auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);
        gs_effect_set_color(gs_effect_get_param_by_name(effect,"color"),color);
        while(gs_effect_loop(effect,"Solid")) {
            gs_render_start(true);
            gs_vertex2f(float(x),float(y));gs_vertex2f(float(x+w),float(y));
            gs_vertex2f(float(x),float(y+h));gs_vertex2f(float(x+w),float(y+h));
            gs_render_stop(GS_TRISTRIP);
        }
    }
    static void outline(double x,double y,double w,double h,double radius,double t,uint32_t color)
    {
        t=std::min(t,std::min(w,h)/2);
        radius=std::clamp(radius,0.0,std::min(w,h)/2);
        const double innerRadius=std::max(0.0,radius-t);
        const int segments=std::clamp(int(std::ceil(radius/2)),8,64);
        constexpr double pi=3.14159265358979323846;
        gs_effect_t *effect=obs_get_base_effect(OBS_EFFECT_SOLID);
        gs_effect_set_color(gs_effect_get_param_by_name(effect,"color"),color);
        while(gs_effect_loop(effect,"Solid")) {
            gs_render_start(true);
            // Paired outer/inner arcs form a one-logical-pixel ring. Repeating
            // the first pair closes the strip without drawing corner handles.
            for(int corner=0;corner<=4;corner++) {
                const int c=corner%4;
                const bool right=c==0||c==1,bottom=c==1||c==2;
                const double ox=right?x+w-radius:x+radius,oy=bottom?y+h-radius:y+radius;
                const double ix=right?x+w-t-innerRadius:x+t+innerRadius;
                const double iy=bottom?y+h-t-innerRadius:y+t+innerRadius;
                for(int i=0;i<=(corner==4?0:segments);i++) {
                    const double angle=(-.5+c*.5+double(i)/segments*.5)*pi;
                    const double dx=std::cos(angle),dy=std::sin(angle);
                    gs_vertex2f(float(ox+radius*dx),float(oy+radius*dy));
                    gs_vertex2f(float(ix+innerRadius*dx),float(iy+innerRadius*dy));
                }
            }
            gs_render_stop(GS_TRISTRIP);
        }
    }
    static void draw(void *opaque,uint32_t cx,uint32_t cy)
    {
        auto *self=static_cast<CropPreview*>(opaque);
        OBSSource parent,filter;double ratio;
        { std::lock_guard<std::mutex> lock(self->stateMutex);
          parent=OBSGetStrongRef(self->weakParent);filter=OBSGetStrongRef(self->weakFilter);ratio=self->pixelRatio; }
        if(!parent||!isCrop(filter)) return;
        const uint32_t sw=obs_source_get_width(parent),sh=obs_source_get_height(parent);
        if(!sw||!sh) return;
        const auto viewport=crop::fit(sw,sh,cx,cy);
        gs_viewport_push();gs_projection_push();gs_matrix_push();
        gs_matrix_identity();
        gs_ortho(0,float(sw),0,float(sh),-100,100);
        gs_set_viewport(int(viewport.left),int(viewport.top),int(viewport.right-viewport.left),int(viewport.bottom-viewport.top));
        const bool srgb=gs_set_linear_srgb(true);
        obs_source_video_render(parent);
        gs_set_linear_srgb(srgb);
        // Only this private display receives the editor graphics.
        gs_set_viewport(0,0,int(cx),int(cy));gs_ortho(0,float(cx),0,float(cy),-100,100);
        const auto r=readCrop(filter);
        if(crop::valid(r)) {
            const double vw=viewport.right-viewport.left,vh=viewport.bottom-viewport.top;
            const double x=viewport.left+r.left*vw,y=viewport.top+r.top*vh;
            const double w=(r.right-r.left)*vw,h=(r.bottom-r.top)*vh;
            OBSDataAutoRelease settings=obs_source_get_settings(filter);
            // Same crop-relative radius as effect_mask_crop.effect, read each
            // frame so slider edits and crop resizing update the guide live.
            const double radius=obs_data_get_double(settings,"effect_mask_crop_corner_radius")/100*std::min(w,h);
            const double t=std::max(1.0,ratio);
            gs_blend_state_push();gs_reset_blend_state();
            outline(x,y,w,h,radius,t,0xff37d4ff);
            const double handle=8*ratio;
            const crop::Point midpoints[]={{x+w/2,y},{x+w,y+h/2},{x+w/2,y+h},{x,y+h/2}};
            for(const auto &p:midpoints) {
                quad(p.x-handle/2-t,p.y-handle/2-t,handle+2*t,handle+2*t,0xff101820);
                quad(p.x-handle/2,p.y-handle/2,handle,handle,0xffe9faff);
            }
            gs_blend_state_pop();
        }
        gs_matrix_pop();gs_projection_pop();gs_viewport_pop();
    }
    OBSSource boundFilter()
    { std::lock_guard<std::mutex> lock(stateMutex);return OBSGetStrongRef(weakFilter); }
    crop::Rect viewport()
    {
        OBSSource p;{std::lock_guard<std::mutex> lock(stateMutex);p=OBSGetStrongRef(weakParent);}
        return p?crop::fit(obs_source_get_width(p),obs_source_get_height(p),width(),height()):crop::Rect{0,0,0,0};
    }
    void updateCursor(int h)
    {
        if(h==(crop::Left|crop::Top)||h==(crop::Right|crop::Bottom)) setCursor(Qt::SizeFDiagCursor);
        else if(h==(crop::Right|crop::Top)||h==(crop::Left|crop::Bottom)) setCursor(Qt::SizeBDiagCursor);
        else if(h&(crop::Left|crop::Right)) setCursor(Qt::SizeHorCursor);
        else if(h&(crop::Top|crop::Bottom)) setCursor(Qt::SizeVerCursor);
        else if(h==crop::Move) setCursor(Qt::SizeAllCursor);
        else unsetCursor();
    }
    void finish(bool cancel)
    {
        if(!dragging) return;
        dragging=false;releaseMouse();
        OBSSource f=OBSGetStrongRef(gestureFilter);gestureFilter=nullptr;
        if(f&&!obs_source_removed(f)) {
            if(cancel) writeCrop(f,startRect);
            // Refresh the properties view's private settings copy once per gesture,
            // so a subsequent radius/feather edit cannot restore stale margins.
            obs_source_update_properties(f);
        }
        activeHandle=crop::None;
    }
protected:
    QPaintEngine *paintEngine() const override {return nullptr;}
    void paintEvent(QPaintEvent *) override {ensureDisplay();}
    void showEvent(QShowEvent *e) override {QWidget::showEvent(e);ensureDisplay();if(display)obs_display_set_enabled(display,true);}
    void hideEvent(QHideEvent *e) override {finish(false);if(display)obs_display_set_enabled(display,false);QWidget::hideEvent(e);}
    void resizeEvent(QResizeEvent *e) override
    { QWidget::resizeEvent(e);finish(false);if(display)obs_display_resize(display,uint32_t(std::lround(width()*devicePixelRatioF())),uint32_t(std::lround(height()*devicePixelRatioF())));{std::lock_guard<std::mutex> lock(stateMutex);pixelRatio=devicePixelRatioF();} }
    bool event(QEvent *e) override
    {
        if(e->type()==QEvent::WindowDeactivate||e->type()==QEvent::FocusOut)finish(false);
        if(e->type()==QEvent::DevicePixelRatioChange) {
            finish(false);
            {std::lock_guard<std::mutex> lock(stateMutex);pixelRatio=devicePixelRatioF();}
            if(display)obs_display_resize(display,uint32_t(std::lround(width()*devicePixelRatioF())),uint32_t(std::lround(height()*devicePixelRatioF())));
        }
        return QWidget::event(e);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if(e->button()!=Qt::LeftButton) return;
        auto f=boundFilter();const auto v=viewport();
        if(!isCrop(f)||v.right<=v.left||v.bottom<=v.top) return;
        const auto p=crop::normalized({e->position().x(),e->position().y()},v);
        startRect=readCrop(f);activeHandle=crop::hit(startRect,p,8/(v.right-v.left),8/(v.bottom-v.top));
        if(!activeHandle) return;
        pressPoint=p;dragging=true;gestureFilter=OBSGetWeakRef(f);setFocus(Qt::MouseFocusReason);grabMouse();e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        auto f=boundFilter();const auto v=viewport();
        if(!isCrop(f)||v.right<=v.left||v.bottom<=v.top){finish(false);return;}
        const auto p=crop::normalized({e->position().x(),e->position().y()},v);
        if(dragging) {
            OBSSource gesture=OBSGetStrongRef(gestureFilter);
            if(gesture!=f){finish(false);return;}
            const auto r=crop::drag(startRect,activeHandle,{p.x-pressPoint.x,p.y-pressPoint.y},2/(v.right-v.left),2/(v.bottom-v.top));
            writeCrop(f,r);e->accept();
        } else updateCursor(crop::hit(readCrop(f),p,8/(v.right-v.left),8/(v.bottom-v.top)));
    }
    void mouseReleaseEvent(QMouseEvent *e) override {if(e->button()==Qt::LeftButton){finish(false);e->accept();}}
    void leaveEvent(QEvent *) override {if(!dragging)unsetCursor();}
    void keyPressEvent(QKeyEvent *e) override
    {
        if(e->key()==Qt::Key_Escape&&dragging){finish(true);e->accept();return;}
        QWidget::keyPressEvent(e);
    }
public:
    explicit CropPreview(QWidget *p):QWidget(p)
    {
        setObjectName(QStringLiteral("cbVisualCropPreview"));
        setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_OpaquePaintEvent);setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);setMinimumHeight(150);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QStringLiteral("局部打码范围编辑预览"));
    }
    ~CropPreview() override
    {
        closing=true;finish(false);
        if(display){obs_display_remove_draw_callback(display,draw,this);obs_display_destroy(display);display=nullptr;}
    }
    bool ensureDisplay()
    {
        if(display)return true;
        if(closing||!isVisible()||width()<1||height()<1) return false;
        gs_init_data info={};info.cx=uint32_t(std::lround(width()*devicePixelRatioF()));info.cy=uint32_t(std::lround(height()*devicePixelRatioF()));
        info.format=GS_BGRA;info.zsformat=GS_ZS_NONE;info.window.hwnd=reinterpret_cast<void*>(winId());
        display=obs_display_create(&info,0xff15171d);
        if(!display)return false;
        {std::lock_guard<std::mutex> lock(stateMutex);pixelRatio=devicePixelRatioF();}
        obs_display_add_draw_callback(display,draw,this);return true;
    }
    bool ready() const {return display!=nullptr;}
    // Diagnostic readback of this editor's draw callback. It captures no desktop
    // windows and uses the same GPU drawing path as the interactive display.
    QImage snapshot()
    {
        const uint32_t cx=uint32_t(std::lround(width()*devicePixelRatioF()));
        const uint32_t cy=uint32_t(std::lround(height()*devicePixelRatioF()));
        if(!cx||!cy)return {};
        QImage result(int(cx),int(cy),QImage::Format_RGBA8888);result.fill(Qt::transparent);
        obs_enter_graphics();
        auto *target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
        auto *stage=gs_stagesurface_create(cx,cy,GS_RGBA);
        if(gs_texrender_begin(target,cx,cy)) {
            vec4 clear;vec4_set(&clear,0.08f,0.09f,0.11f,1);gs_clear(GS_CLEAR_COLOR,&clear,0,0);
            draw(this,cx,cy);gs_texrender_end(target);
            gs_stage_texture(stage,gs_texrender_get_texture(target));
            uint8_t *mapped=nullptr;uint32_t stride=0;
            if(gs_stagesurface_map(stage,&mapped,&stride)) {
                for(uint32_t y=0;y<cy;y++)memcpy(result.scanLine(int(y)),mapped+y*stride,cx*4);
                gs_stagesurface_unmap(stage);
            } else result={};
        } else result={};
        gs_stagesurface_destroy(stage);gs_texrender_destroy(target);obs_leave_graphics();
        return result;
    }
    void bind(obs_source_t *f)
    {
        auto old=boundFilter();if(old==f)return;
        finish(false);
        std::lock_guard<std::mutex> lock(stateMutex);
        weakFilter=f?OBSGetWeakRef(f):OBSWeakSource{};
        obs_source_t *p=f?obs_filter_get_parent(f):nullptr;
        weakParent=p?OBSGetWeakRef(p):OBSWeakSource{};
    }
};

class Editor final:public QObject {
    QPointer<QDialog> dialog;
    QPointer<QWidget> original;
    QPointer<CropPreview> preview;
    QPointer<QLabel> hint;
    QPointer<QFormLayout> cropForm;
    QPointer<QLabel> marker;
    QString boundUuid;
    bool inserted=false;
    void restoreOriginal()
    {
        if(inserted&&original&&preview&&original->parentWidget()&&original->parentWidget()->layout()) {
            delete original->parentWidget()->layout()->replaceWidget(preview,original);
            inserted=false;
        }
        if(preview)preview->hide();
        if(original)original->show();
    }
    void restoreRows()
    {
        if(cropForm)for(int row=1;row<=4&&row<cropForm->rowCount();row++)cropForm->setRowVisible(row,true);
        cropForm=nullptr;marker=nullptr;
    }
public:
    Editor(QDialog *d,QWidget *o):QObject(d),dialog(d),original(o)
    {setObjectName(QStringLiteral("cbCropEditorController"));}
    ~Editor() override
    {
        restoreRows();
        restoreOriginal();
        if(preview){delete preview;preview=nullptr;}
        if(hint)delete hint;
        if(original&&!dialog.isNull()&&!dialog->property("cbClosing").toBool())original->show();
    }
    void sync(QLabel *label,const QString &uuid)
    {
        OBSSourceAutoRelease f=uuid.isEmpty()?nullptr:obs_get_source_by_uuid(uuid.toUtf8().constData());
        const bool active=isCrop(f);
        if(!active){restoreRows();boundUuid.clear();if(preview)preview->bind(nullptr);if(hint)hint->hide();restoreOriginal();return;}
        if(!original||!original->parentWidget()||!original->parentWidget()->layout())return;
        // Only migrate the old exact default instance name, never custom names.
        if(strcmp(obs_source_get_name(f),"Composite Blur")==0)
            obs_source_set_name(f,obs_source_get_display_name(filterId));
        auto *layout=original->parentWidget()->layout();
        if(!preview){
            preview=new CropPreview(original->parentWidget());
            // Keep the original preview alive for OBS's own close callbacks.
            original->hide();
            hint=new QLabel(original->parentWidget());hint->setWordWrap(true);hint->setObjectName(QStringLiteral("cbCropEditorHint"));
            layout->addWidget(hint);
        }
        if(!inserted){delete layout->replaceWidget(original,preview);inserted=true;}
        original->hide();preview->bind(f);preview->show();preview->ensureDisplay();
        if(!preview->ready()){restoreOriginal();restoreRows();return;}
        if(marker!=label){restoreRows();marker=label;cropForm=qobject_cast<QFormLayout*>(label->parentWidget()->layout());}
        if(cropForm){cropForm->setRowVisible(0,false);for(int row=1;row<=4&&row<cropForm->rowCount();row++)cropForm->setRowVisible(row,false);}
        const bool valid=crop::valid(readCrop(f));
        hint->setText(valid?QString():QStringLiteral("当前裁剪范围为空，请使用下方数值修正，或恢复预设值。"));
        // Preserve a repair path for old settings whose opposing margins sum to >=100%.
        if(!valid&&cropForm)for(int row=1;row<=4&&row<cropForm->rowCount();row++)cropForm->setRowVisible(row,true);
        hint->setVisible(!valid);boundUuid=uuid;
    }
    bool eventFilter(QObject *watched,QEvent *e) override
    {
        if(watched==dialog&&(e->type()==QEvent::Close)){dialog->setProperty("cbClosing",true);if(preview){delete preview;preview=nullptr;}}
        return QObject::eventFilter(watched,e);
    }
};

class Watcher final:public QObject {
    QTimer timer;
    QList<QPointer<Editor>> editors;
public:
    explicit Watcher(QObject *p):QObject(p)
    {
        connect(&timer,&QTimer::timeout,this,[this](){scan();});timer.start(100);
    }
    ~Watcher() override {timer.stop();for(auto e:editors)if(e)delete e;}
    void scan()
    {
        for(auto *w:QApplication::topLevelWidgets()) {
            auto *d=qobject_cast<QDialog*>(w);
            if(!d||!d->isVisible()||d->objectName()!=QStringLiteral("OBSBasicFilters"))continue;
            auto *e=dynamic_cast<Editor*>(d->findChild<QObject*>(QStringLiteral("cbCropEditorController"),Qt::FindDirectChildrenOnly));
            QLabel *marker=nullptr;QString uuid;
            auto *properties=d->findChild<QWidget*>(QStringLiteral("propertiesFrame"));
            if(properties)for(auto *label:properties->findChildren<QLabel*>()) {
                const QString text=label->text();const auto pos=text.indexOf(markerPrefix);
                if(pos<0||!label->parentWidget()->isVisibleTo(d))continue;
                const QString value=text.mid(pos+markerPrefix.size(),36);
                if(value.size()==36){marker=label;uuid=value;break;}
            }
            if(!e&&!uuid.isEmpty()) {
                auto *original=d->findChild<QWidget*>(QStringLiteral("preview"));
                if(!original)continue;
                e=new Editor(d,original);d->installEventFilter(e);editors.append(e);
            }
            if(e)e->sync(marker,uuid);
        }
    }
};
QPointer<Watcher> watcher;
}

extern "C" void cb_crop_ui_start(void)
{
    if(!qApp||watcher)return;
    const QString version=QString::fromUtf8(obs_get_version_string());
    if(!version.startsWith(QStringLiteral("32.2.2"))){blog(LOG_WARNING,"[Composite Blur] visual editor supports OBS 32.2.2; retaining numeric crop controls");return;}
    if(QThread::currentThread()!=qApp->thread())return;
    watcher=new Watcher(qApp);
    blog(LOG_INFO,"[Composite Blur] local visual crop editor enabled (OBS 32.2.2, upstream 1.6.1)");
}
extern "C" void cb_crop_ui_stop(void)
{
    if(watcher&&qApp&&QThread::currentThread()==qApp->thread()){delete watcher;watcher=nullptr;}
}
extern "C" __declspec(dllexport) bool cb_crop_ui_capture_preview(void *widget,const char *path)
{
    if(!qApp||QThread::currentThread()!=qApp->thread()||!widget||!path)return false;
    auto *preview=dynamic_cast<CropPreview*>(static_cast<QWidget*>(widget));
    return preview&&preview->snapshot().save(QString::fromUtf8(path));
}
