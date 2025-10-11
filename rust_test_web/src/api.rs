use actix_web::{get, post, web, HttpResponse, Responder};
use serde::{Deserialize, Serialize};
use crate::db::DbPool;
use chrono::{NaiveDateTime, NaiveTime};
use sqlx::Row;


#[derive(Deserialize)]
struct UploadData {
    value1: String,  // uid
    value2: String,  // direction
    value3: String,  // device_name
    value4: Option<String>, // device_time (nullable)
}


#[derive(Serialize, sqlx::FromRow)]
struct LogEntry {
    id: i64,
    uid: String,
    direction: String,
    device_name: String,
    device_time: Option<NaiveTime>, // ✅ 正確對應 MySQL TIME
    authorized: i32,                // ✅ 對應 TINYINT(1)
    username: Option<String>,
}



#[post("/upload")]
async fn upload(data: web::Json<UploadData>, db: web::Data<DbPool>) -> impl Responder {
    let UploadData { value1, value2, value3, value4 } = data.into_inner();

    let row: Option<(String,)> = sqlx::query_as("SELECT username FROM authorized_uids WHERE uid = ?")
        .bind(&value1)
        .fetch_optional(db.get_ref())
        .await
        .ok()
        .flatten();

    let (is_authorized, username) = match row {
        Some((name,)) => (true, name),
        None => (false, "未知".to_string()),
    };
        // 解析時間（只保留時間部分）
    let parsed_time: Option<NaiveTime> = match value4 {
        Some(ref dt_str) => {
            match NaiveDateTime::parse_from_str(dt_str, "%Y-%m-%dT%H:%M") {
                Ok(dt) => Some(dt.time()), // ✅ 成功就取時間部分
                Err(_) => None,            // ❌ 失敗就當作空值（避免錯誤）
            }
        }
        None => None,
    };

    let result = sqlx::query(
        "INSERT INTO access_logs (uid, direction, device_name, device_time, authorized) VALUES (?, ?, ?, ?, ?)"
    )
    .bind(&value1)
    .bind(&value2)
    .bind(&value3)
    .bind(parsed_time) // ✅ 是 Option<NaiveTime>
// 已是 Option<String>
    .bind(is_authorized as i32)
    .execute(db.get_ref())
    .await;

    match result {
        Ok(_) => HttpResponse::Ok().json(serde_json::json!({
            "success": true,
            "authorized": is_authorized,
            "user": username
        })),
        Err(e) => HttpResponse::Ok().json(serde_json::json!({
            "success": false,
            "error": format!("寫入錯誤: {}", e)
        })),
    }
}

#[get("/logs")]
async fn get_logs(db: web::Data<DbPool>) -> impl Responder {
    let rows = sqlx::query(
        r#"
        SELECT 
            access_logs.id,
            access_logs.uid,
            access_logs.direction,
            access_logs.device_name,
            access_logs.device_time,
            access_logs.authorized,
            authorized_uids.username AS username
        FROM access_logs
        LEFT JOIN authorized_uids ON access_logs.uid = authorized_uids.uid
        ORDER BY access_logs.id DESC
        LIMIT 50
        "#
    )
    .try_map(|row: sqlx::mysql::MySqlRow| {
        Ok(LogEntry {
            id: row.get("id"),
            uid: row.get("uid"),
            direction: row.get("direction"),
            device_name: row.get("device_name"),
            device_time: row.try_get("device_time")?, // Option<NaiveTime>
            authorized: row.get("authorized"),
            username: row.try_get("username").ok(), // Option<String>
        })
    })
    .fetch_all(db.get_ref())
    .await;

    match rows {
        Ok(logs) => HttpResponse::Ok().json(logs),
        Err(e) => HttpResponse::InternalServerError().body(format!("讀取錯誤: {}", e)),
    }
}




pub fn init_routes(cfg: &mut web::ServiceConfig) {
    cfg.service(upload);
    cfg.service(get_logs);
}
