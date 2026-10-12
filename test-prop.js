const X = 1;
try {
  Object.defineProperty(global, "X", { value: X, writable: false });
  console.log("Success");
} catch (e) {
  console.log("Error:", e.message);
}
