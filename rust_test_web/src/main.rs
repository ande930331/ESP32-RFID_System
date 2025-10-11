use actix_web::{App, HttpServer, web};  // ✅ 加了 web
use actix_cors::Cors;

mod db;
mod api;

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    dotenv::dotenv().ok();
    let db_pool = db::init_db().await.expect("❌ 資料庫連線失敗");

    HttpServer::new(move || {
        let cors = Cors::permissive();

        App::new()
            .wrap(cors)
            .app_data(web::Data::new(db_pool.clone()))
            .configure(api::init_routes)
    })
    .bind(("0.0.0.0", 4000))?
    .run()
    .await
}
