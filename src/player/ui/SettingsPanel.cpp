#include "SettingsPanel.h"
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <algorithm>
namespace adv_walkman { namespace player {
namespace {
const char* times[]={"15秒","30秒","1分钟","3分钟","5分钟","10分钟","永不"};
constexpr uint16_t bg=0x0861,accent=0xFBE0;
}
bool SettingsPanel::handle(UiAction a,PlayerRuntime& player){
    if(returning())return true;
    if(a==UiAction::Back){if(panel_){panel_=0;selected_=0;invalidate();return true;}store.saveSoon(millis());return false;}
    if(panel_==2){if(a==UiAction::Confirm){panel_=0;invalidate();}return true;}
    if(panel_==3){
        if(a==UiAction::Left||a==UiAction::Right){confirmed_=!confirmed_;invalidate();}
        if(a==UiAction::Confirm){
            if(!confirmed_){panel_=0;invalidate();return true;}
            ++returnRequests;returnError_="none";
            // Cardio/Launcher TEST route only. Never guess offsets or another app.
            target_=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_TEST,nullptr);
            const auto* running=esp_ota_get_running_partition();esp_app_desc_t description{};
            if(!target_||!running||target_->address==running->address||esp_ota_get_partition_description(target_,&description)!=ESP_OK){failReturn("launcher_target");return true;}
            if(player.snapshot().state==PlayerState::Playing&&!player.pause()){failReturn("launcher_pause");return true;}
            if(!player.stateStoreAvailable()){failReturn("launcher_state_store");return true;}
            player.requestCheckpoint();store.saveSoon(millis());returnAt_=millis();returnState_=1;invalidate();
        }return true;
    }
    const int count=panel_==1?2:4;
    if(a==UiAction::Up)selected_=(selected_+count-1)%count;
    if(a==UiAction::Down)selected_=(selected_+1)%count;
    bool changed=false;
    const int delta=a==UiAction::Right?1:a==UiAction::Left?-1:0;
    if(delta && panel_==0 && selected_==0){const auto old=store.value.brightness;
        store.value.brightness=std::max(10,std::min(100,int(old)+delta*10));changed=old!=store.value.brightness;
    }else if(delta&&panel_==1){auto& index=selected_==0?store.value.playerTimeout:store.value.otherTimeout;index=(index+7+delta)%7;changed=true;}
    if(a==UiAction::Confirm && panel_==0){
        if(selected_==1){panel_=1;selected_=0;}
        else if(selected_==2)panel_=2;
        else if(selected_==3){panel_=3;confirmed_=false;}
    }
    if(changed){++changes;store.changed(millis());}
    invalidate();return true;
}
void SettingsPanel::failReturn(const char* reason){returnError_=reason;returnState_=3;++returnErrors;invalidate();}
void SettingsPanel::service(PlayerRuntime& player,bool logIdle){
    store.service(millis(),player.persistenceIdle()&&logIdle);
    if(lastWrites_!=store.writes||lastErrors_!=store.errors){lastWrites_=store.writes;lastErrors_=store.errors;invalidate();}
    if(returnState_==1){
        if(store.error()[0]!='n'){failReturn(store.error());return;}
        if(player.persistenceIdle()&&store.idle()){
            if(player.lastPersistenceResult()!=PersistenceResult::Ok)failReturn("launcher_save");else{returnState_=2;invalidate();}
        }else if(millis()-returnAt_>10000)failReturn("launcher_save_timeout");
    }
    if(returnState_==2&&millis()-returnAt_>15000)failReturn("launcher_log_timeout");
}
void SettingsPanel::finishReturn(bool logOk){
    if(returnState_!=2)return;
    if(!logOk){failReturn("launcher_log");return;}
    // This API validates the image and changes only the normal boot selection.
    if(esp_ota_set_boot_partition(target_)!=ESP_OK){failReturn("launcher_boot_select");return;}
    esp_restart();
}
bool SettingsPanel::prepare(FontCache& fonts){
    const char* fixed="设置屏幕亮度息屏时间关于返回播放器其他页面秒分钟永不取消确认上下选择左右调整返回保存中未保存已保存失败请按进入菜单设备版本0123456789%: /Enter Esc Launcher ADV Walkman M5Stack Cardputer";
    if(!fonts.requestUiWindow(fixed,12,0,3000,1))return false;
    return fonts.requestUiWindow(ADV_WALKMAN_VERSION,12,0,123,1)&&fonts.requestUiWindow(returnError_,12,0,240,1);
}
bool SettingsPanel::render(M5GFX& display,M5Canvas& row,FontCache& fonts){
    if(complete())return false;
    const int h=std::min(18,240-stripe_);row.clearClipRect();row.setClipRect(0,0,135,h);row.fillScreen(bg);
    CachedUiFont face(&fonts,12);row.setFont(&face);row.setTextSize(1);
    auto text=[&](const char* s,int y,bool highlight=false){row.setTextColor(highlight?accent:TFT_WHITE,bg);row.setCursor(6,y-stripe_);row.print(s);};
    text("设置",7,true);row.drawFastHLine(6,25-stripe_,123,0x8410);
    if(panel_==0){char brightness[40];std::snprintf(brightness,sizeof(brightness),"屏幕亮度 %u%%",store.value.brightness);
        const char* labels[]={brightness,"息屏时间","关于","返回 Launcher"};
        for(int i=0;i<4;++i){const int y=44+i*31;if(selected_==i)row.drawRect(3,y-5-stripe_,129,24,accent);text(labels[i],y,selected_==i);}
    }else if(panel_==1){text("息屏时间",38,true);const char* labels[]={"播放器","其他页面"};
        for(int i=0;i<2;++i){const int y=66+i*46;if(selected_==i)row.drawRect(3,y-4-stripe_,129,39,accent);text(labels[i],y,selected_==i);text(times[i?store.value.otherTimeout:store.value.playerTimeout],y+17);}
    }else if(panel_==2){text("ADV Walkman",43,true);text(ADV_WALKMAN_VERSION,67);text("M5Stack",103);text("Cardputer ADV",123);
    }else{ text("返回 Launcher",43,true);text("取消",80,!confirmed_);text("确认",107,confirmed_);
        text("请按 Enter",145);text("进入菜单",162); }
    if(returning())text("保存中",185,true);
    else if(returnState_==3){text("返回失败",169,true);text(returnError_,187);}
    else text(store.error()[0]!='n'?"未保存":store.idle()?"已保存":"保存中",187,true);
    text("上下选择 左右调整",210);text("Esc 返回",226);
    display.setClipRect(0,stripe_,135,h);row.pushSprite(&display,0,stripe_);display.clearClipRect();row.clearClipRect();row.setFont(&fonts::Font0);
    stripe_+=h;if(complete())++frames;return true;
}
} }
