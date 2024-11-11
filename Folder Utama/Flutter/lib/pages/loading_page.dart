import 'package:flutter/material.dart';
import 'package:lottie/lottie.dart';

class LoadingPage extends StatelessWidget {
  const LoadingPage({super.key});

  @override
  Widget build(BuildContext context) {
    // Menampilkan loading selama 3 detik
    Future.delayed(const Duration(seconds: 3), () {
      // Setelah 3 detik, navigasi ke halaman yang dituju
      Navigator.of(context).pop(); // Kembali ke halaman sebelumnya
    });

    return Scaffold(
      backgroundColor: Colors.white,
      body: Center(
        child: Lottie.asset(
          'assets/aquascape_loading.json', // Pastikan path sesuai
          width: 200,
          height: 200,
          fit: BoxFit.fill,
        ),
      ),
    );
  }
} 