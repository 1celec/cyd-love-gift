#pragma once
// ============ عدّل هنا فقط ============
#define HER_NAME      "My Love"        // الاسم اللي يظهر في التحية
#define TITLE_TEXT    "Forever Yours"  // العنوان تحت القلب

// تاريخ بدايتكم (لعدّاد الأيام)
#define ANNIV_Y 2024
#define ANNIV_M 2
#define ANNIV_D 14

// الواي فاي يُستخدم مرة واحدة عند التشغيل فقط لضبط الوقت (اختياري)
// لو تركته فارغاً: العدّاد يتحول إلى "Forever & always"
#define WIFI_SSID     ""
#define WIFI_PASS     ""
#define TZ_OFFSET_SEC 10800            // السعودية +3 ساعات

#define LED_GLOW  1                    // نبض وردي بالـ LED
#define AUTO_DIM  1                    // خفض السطوع تلقائياً في الظلام
#define LDR_INVERT 1                   // بدّلها لو الإضاءة معكوسة

// الرسائل (يفضّل أقل من 60 حرف)
static const char* const NOTES[] = {
  "You are my favorite hello and my hardest goodbye.",
  "Every day with you is my favorite day.",
  "You make my heart smile without even trying.",
  "Home is wherever I am with you.",
  "I fall for you a little more every day.",
  "Thank you for being you. I love you endlessly.",
  "My heart chose you, and it would choose you again.",
  "With you, ordinary moments become my favorite memories.",
  "You are the sweetest part of my day.",
  "Be my forever, my love, my always."
};

// القسائم (6 بالضبط)
static const char* const COUPONS[6] = {
  "A big warm hug",
  "Dinner, your choice",
  "Movie night, your pick",
  "A sweet surprise",
  "Breakfast in bed",
  "A love letter just for you"
};
