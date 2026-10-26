// src/pages/*.html are bundled as text (see the rules in wrangler.toml).
declare module "*.html" {
  const html: string;
  export default html;
}
