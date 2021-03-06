#include <vector>

#include "caffe/sgd_solvers.hpp"

namespace caffe {

template <typename Dtype>
void SantaeSolver<Dtype>::SantaePreSolve() {
  // Add the extra history entries for Santa after those from
  // SGDSolver::PreSolve
  const vector<Blob<Dtype>*>& net_params = this->net_->learnable_params();
  for (int i = 0; i < net_params.size(); ++i) {//G_t
    const vector<int>& shape = net_params[i]->shape();
    this->history_.push_back(
      shared_ptr<Blob<Dtype> >(new Blob<Dtype>(shape)));
  }
  for (int i = 0; i < net_params.size(); ++i) {//alpha_t
    const vector<int>& shape = net_params[i]->shape();
    this->history_.push_back(
      shared_ptr<Blob<Dtype> >(new Blob<Dtype>(shape)));
  }
  for (int i = 0; i < net_params.size(); ++i) {//u_t
    const vector<int>& shape = net_params[i]->shape();
    this->history_.push_back(
      shared_ptr<Blob<Dtype> >(new Blob<Dtype>(shape)));
  }
  if(this->param_.approx_g()){
    for (int i = 0; i < net_params.size(); ++i) {//G_t-1
      const vector<int>& shape = net_params[i]->shape();
      this->history_.push_back(
        shared_ptr<Blob<Dtype> >(new Blob<Dtype>(shape)));
    }
  }
}

template <typename Dtype>
void SantaeSolver<Dtype>::ComputeUpdateValue(int param_id, Dtype rate) {
  const vector<Blob<Dtype>*>& net_params = this->net_->learnable_params();
  const vector<float>& net_params_lr = this->net_->params_lr();
  Dtype local_rate = rate * net_params_lr[param_id];
  const Dtype beta2 = this->param_.sigma();
  const Dtype C = this->param_.c();

  // we create aliases for convenience
  size_t update_history_offset = net_params.size();
  Blob<Dtype>* V_t = this->history_[param_id].get();
  Blob<Dtype>* G_t = this->history_[param_id + update_history_offset].get();
  Blob<Dtype>* alpha_t = this->history_[param_id + 2*update_history_offset].get();
  Blob<Dtype>* u_t = this->history_[param_id + 3*update_history_offset].get();
  Blob<Dtype>* val_t = this->temp_[param_id].get();
  Blob<Dtype>* G_t_ = NULL;
  // if use approximate calculation for \nabla G_t
  if(this->param_.approx_g()){
    G_t_ = this->history_[param_id + 4*update_history_offset].get();
  }
  int explore_iter = this->param_.explore();
  int nD = this->param_.nd();
  int update_u = this->param_.update_u();

  const int t = this->iter_  + 1;
  const int N = net_params[param_id]->count();
  const Dtype eps_hat = this->param_.lambda();
  // stepsize = local_rate / nD
  Dtype eta_ = Dtype(sqrt(local_rate/nD));
  Dtype anneal = Dtype(this->param_.anneal_a()*pow(t+this->param_.anneal_b(), this->param_.anneal_c()));

  switch (Caffe::mode()) {
    case Caffe::CPU: {
    // initilization
    if(t == 1) {
      caffe_add_scalar(N, Dtype(C * eta_), alpha_t->mutable_cpu_data());
      caffe_rng_gaussian(N, Dtype(0), Dtype(1),
        u_t->mutable_cpu_data());
      caffe_cpu_scale(N, eta_, u_t->cpu_data(), u_t->mutable_cpu_data());
      // if not use preconditioners, initialize it to identity
      if(this->param_.pc() == 0){
          caffe_set(N, Dtype(1), G_t->mutable_cpu_data());
      }
    }

    // update preconditioner
    if(this->param_.pc()){
      caffe_mul(N,
        net_params[param_id]->cpu_diff(),
        net_params[param_id]->cpu_diff(),
        val_t->mutable_cpu_data());
      caffe_cpu_axpby(N, Dtype(1)-beta2,
        val_t->cpu_data(), beta2,
        V_t->mutable_cpu_data());

      // set update
      caffe_powx(N,
        V_t->cpu_data(), Dtype(0.5),
        val_t->mutable_cpu_data());
      caffe_add_scalar(N, eps_hat, val_t->mutable_cpu_data());
      caffe_powx(N,
        val_t->cpu_data(), Dtype(-0.5),
        G_t->mutable_cpu_data());
    }

    // update alpha
    if(t < explore_iter || update_u){
      caffe_mul(N, u_t->cpu_data(), u_t->cpu_data(),
        val_t->mutable_cpu_data());
      caffe_add_scalar(N, -local_rate/anneal/nD, val_t->mutable_cpu_data());
      caffe_cpu_axpby(N, Dtype(1),
        val_t->cpu_data(), Dtype(1),
        alpha_t->mutable_cpu_data());
    }
    if(t < explore_iter && this->param_.approx_g() && t > 1 && this->param_.pc()){
      caffe_div(N, G_t_->cpu_data(), G_t->cpu_data(), G_t_->mutable_cpu_data());
      caffe_add_scalar(N, Dtype(-1), G_t_->mutable_cpu_data());
      caffe_div(N, G_t_->cpu_data(), u_t->cpu_data(), G_t_->mutable_cpu_data());
      caffe_cpu_scale(N, Dtype(-local_rate/nD/anneal), G_t_->cpu_data(), 
        G_t_->mutable_cpu_data());
    }
    // update u
    caffe_mul(N, alpha_t->cpu_data(), u_t->cpu_data(),
      val_t->mutable_cpu_data());
    caffe_cpu_axpby(N, Dtype(-1),
      val_t->cpu_data(), Dtype(1),
      u_t->mutable_cpu_data());
    caffe_mul(N, G_t->cpu_data(), net_params[param_id]->cpu_diff(),
      val_t->mutable_cpu_data());
    caffe_cpu_axpby(N, local_rate,
      val_t->cpu_data(), Dtype(1),
      u_t->mutable_cpu_data());
    // add Gaussian noise
    if(t < explore_iter){
      caffe_rng_gaussian(N, Dtype(0), Dtype(1),
        val_t->mutable_cpu_data());
      caffe_powx(N, val_t->cpu_data(), Dtype(2), val_t->mutable_cpu_data());
      caffe_mul(N, val_t->cpu_data(), G_t->cpu_data(),
        val_t->mutable_cpu_data());
      caffe_cpu_scale(N, Dtype(2*pow(local_rate/nD, 1.5)/anneal), val_t->cpu_data(), 
        val_t->mutable_cpu_data());
      caffe_powx(N, val_t->cpu_data(), Dtype(0.5), val_t->mutable_cpu_data());
      caffe_cpu_axpby(N, Dtype(1),
        val_t->cpu_data(), Dtype(1), u_t->mutable_cpu_data());
      if(this->param_.approx_g()){
        if(t > 1){
          caffe_cpu_axpby(N, Dtype(1),
            G_t_->cpu_data(), Dtype(1), u_t->mutable_cpu_data());
        }
        caffe_cpu_axpby(N, Dtype(1), G_t->cpu_data(), Dtype(0), G_t_->mutable_cpu_data());
      }
    }

    caffe_mul(N, G_t->cpu_data(), u_t->cpu_data(),
      net_params[param_id]->mutable_cpu_diff());
    break;
  }
  case Caffe::GPU: {
#ifndef CPU_ONLY
	  if(t == 1) {
		  caffe_gpu_add_scalar(N, Dtype(C * eta_), alpha_t->mutable_gpu_data());
		  caffe_gpu_rng_gaussian(N, Dtype(0), Dtype(1),
        u_t->mutable_gpu_data());
		  caffe_gpu_scale(N, eta_, u_t->gpu_data(), u_t->mutable_gpu_data());
      if(this->param_.pc() == 0){
        caffe_gpu_set(N, Dtype(1), G_t->mutable_gpu_data());
      }
	  }

    if(this->param_.pc()){
    	caffe_gpu_mul(N,
    		net_params[param_id]->gpu_diff(),
    		net_params[param_id]->gpu_diff(),
    		val_t->mutable_gpu_data());
    	caffe_gpu_axpby(N, Dtype(1)-beta2,
    		val_t->gpu_data(), beta2,
    		V_t->mutable_gpu_data());

    	// set update
    	caffe_gpu_powx(N,
    		V_t->gpu_data(), Dtype(0.5),
    		val_t->mutable_gpu_data());
    	caffe_gpu_add_scalar(N, eps_hat, val_t->mutable_gpu_data());
    	caffe_gpu_powx(N,
    		val_t->gpu_data(), Dtype(-0.5),
    		G_t->mutable_gpu_data());
    }

    if(t < explore_iter || update_u){
    	caffe_gpu_mul(N, u_t->gpu_data(), u_t->gpu_data(),
    		val_t->mutable_gpu_data());
    	caffe_gpu_add_scalar(N, -local_rate/anneal/nD, val_t->mutable_gpu_data());
    	caffe_gpu_axpby(N, Dtype(1),
    		val_t->gpu_data(), Dtype(1),
    		alpha_t->mutable_gpu_data());
    }
    if(t < explore_iter && this->param_.approx_g() && t > 1 && this->param_.pc()){
      caffe_gpu_div(N, G_t_->gpu_data(), G_t->gpu_data(), G_t_->mutable_gpu_data());
    	caffe_gpu_add_scalar(N, Dtype(-1), G_t_->mutable_gpu_data());
    	caffe_gpu_div(N, G_t_->gpu_data(), u_t->gpu_data(), G_t_->mutable_gpu_data());
    	caffe_gpu_scale(N, Dtype(-local_rate/nD/anneal), 
        G_t_->gpu_data(), G_t_->mutable_gpu_data());
    }
  	caffe_gpu_mul(N, alpha_t->gpu_data(), u_t->gpu_data(),
  		val_t->mutable_gpu_data());
  	caffe_gpu_axpby(N, Dtype(-1),
  		val_t->gpu_data(), Dtype(1),
  		u_t->mutable_gpu_data());
  	caffe_gpu_mul(N, G_t->gpu_data(), net_params[param_id]->gpu_diff(),
  		val_t->mutable_gpu_data());
  	caffe_gpu_axpby(N, local_rate,
  		val_t->gpu_data(), Dtype(1),
  		u_t->mutable_gpu_data());
    if(t < explore_iter){
    	caffe_gpu_rng_gaussian(N, Dtype(0), Dtype(1),
        val_t->mutable_gpu_data());
    	caffe_gpu_powx(N, val_t->gpu_data(), Dtype(2), val_t->mutable_gpu_data());
    	caffe_gpu_mul(N, val_t->gpu_data(), G_t->gpu_data(),
        val_t->mutable_gpu_data());
    	caffe_gpu_scale(N, Dtype(2*pow(local_rate/nD, 1.5)/anneal), val_t->gpu_data(), 
        val_t->mutable_gpu_data());
    	caffe_gpu_powx(N, val_t->gpu_data(), Dtype(0.5), val_t->mutable_gpu_data());
    	caffe_gpu_axpby(N, Dtype(1),
        val_t->gpu_data(), Dtype(1), u_t->mutable_gpu_data());
    	if(this->param_.approx_g()){
        if(t > 1){
      		caffe_gpu_axpby(N, Dtype(1),
            G_t_->gpu_data(), Dtype(1), u_t->mutable_gpu_data());
      	}
      	caffe_gpu_axpby(N, Dtype(1), G_t->gpu_data(), Dtype(0), G_t_->mutable_gpu_data());
      }
    }

  	caffe_gpu_mul(N, G_t->gpu_data(), u_t->gpu_data(),
      net_params[param_id]->mutable_gpu_diff());

#else
    NO_GPU;
#endif
    break;
  }
  default:
    LOG(FATAL) << "Unknown caffe mode: " << Caffe::mode();
  }
}

INSTANTIATE_CLASS(SantaeSolver);
REGISTER_SOLVER_CLASS(Santae);

}  // namespace caffe
