use sqlx::{mysql::MySqlPoolOptions, MySqlPool};
use std::env;

pub type DbPool = MySqlPool;
pub async fn init_db() -> Result<DbPool, sqlx::Error> {
    let database_url = env::var("DATABASE_URL").expect("❌ 環境變數 DATABASE_URL 未設定");
    MySqlPoolOptions::new()
        .max_connections(5)
        .connect(&database_url)
        .await
}
